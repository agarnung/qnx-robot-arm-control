/*
 * main.c — control_core entry point.
 *
 * control_core is the "núcleo de control" component: it is the only part
 * of visual_control_robot that talks to the physical robot socket. The
 * vision component (unchanged, Python/MediaPipe, scripts) now talks to
 * this process over the IPC endpoint defined by ipc_transport.h instead of
 * opening src/robot/connection.py's socket itself.
 *
 * Usage:
 *   control_core --endpoint /tmp/vcr_control.sock --robot-ip 192.168.2.46
 *                [--dry-run] [--with-telemetry] [--telemetry-timeout-ms 20]
 *                [--log latency.csv] [--period-s 0.15]
 *
 * --dry-run runs the full request/reply/IPC-latency loop without ever
 * opening a socket to the robot — useful for the Linux-vs-QNX
 * latency comparison, since it isolates "cost of the control loop + IPC"
 * from "cost of the robot's own TCP link", and lets this be exercised
 * without hardware at all via tools/bench_client.
 */
#define _POSIX_C_SOURCE 200809L
#include "ipc_transport.h"
#include "robot_link.h"
#include "latency_log.h"
#include "clock_util.h"
#include "urscript.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

typedef struct {
    const char *endpoint;
    const char *robot_ip;
    const char *log_path;
    int   dry_run;
    int   with_telemetry;
    int   telemetry_timeout_ms;
    double period_s;
} cli_opts_t;

static void print_usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s --endpoint PATH [--robot-ip IP] [--dry-run]\n"
        "       [--with-telemetry] [--telemetry-timeout-ms N] [--log PATH]\n"
        "       [--period-s SECONDS]\n",
        argv0);
}

static int parse_args(int argc, char **argv, cli_opts_t *o) {
    memset(o, 0, sizeof(*o));
    o->endpoint = "/tmp/vcr_control.sock";
    o->telemetry_timeout_ms = 20;
    o->period_s = 0.05;  /* 20 Hz, the rate used by bench_client */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--endpoint") && i + 1 < argc) {
            o->endpoint = argv[++i];
        } else if (!strcmp(argv[i], "--robot-ip") && i + 1 < argc) {
            o->robot_ip = argv[++i];
        } else if (!strcmp(argv[i], "--log") && i + 1 < argc) {
            o->log_path = argv[++i];
        } else if (!strcmp(argv[i], "--dry-run")) {
            o->dry_run = 1;
        } else if (!strcmp(argv[i], "--with-telemetry")) {
            o->with_telemetry = 1;
        } else if (!strcmp(argv[i], "--telemetry-timeout-ms") && i + 1 < argc) {
            o->telemetry_timeout_ms = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--period-s") && i + 1 < argc) {
            o->period_s = atof(argv[++i]);
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "unrecognised argument: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(2);
        }
    }
    if (!o->robot_ip && !o->dry_run) {
        fprintf(stderr, "error: --robot-ip is required unless --dry-run is set\n");
        print_usage(argv[0]);
        exit(2);
    }
    return 0;
}

int main(int argc, char **argv) {
    cli_opts_t opts;
    parse_args(argc, argv, &opts);

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    ipc_server_t *srv = ipc_server_open(opts.endpoint);
    if (!srv) {
        perror("ipc_server_open");
        return 1;
    }
    fprintf(stderr, "[control_core] listening on %s (IPC backend selected at "
                     "build time — see include/ipc_transport.h)\n",
            opts.endpoint);

    robot_link_t *robot = NULL;
    if (!opts.dry_run) {
        fprintf(stderr, "[control_core] connecting to robot at %s:30003 ...\n",
                opts.robot_ip);
        robot = robot_link_connect(opts.robot_ip);
        if (!robot) {
            perror("robot_link_connect");
            fprintf(stderr, "[control_core] continuing in degraded mode: "
                             "commands will be accepted and timestamped but "
                             "NOT sent to any robot\n");
        } else {
            fprintf(stderr, "[control_core] robot link established\n");
        }
    } else {
        fprintf(stderr, "[control_core] --dry-run: no robot socket will be opened\n");
    }

    latency_log_t *log = latency_log_open(opts.log_path);

    double prev_motion_recv_s = 0.0;
    char script[256];

    fprintf(stderr, "[control_core] ready, waiting for the vision process...\n");

    while (!g_stop) {
        vcr_cmd_msg_t cmd;
        ipc_request_ctx_t *ctx;

        if (ipc_server_receive(srv, &cmd, &ctx) != 0) {
            if (g_stop) break;
            fprintf(stderr, "[control_core] client disconnected, waiting for a new one\n");
            continue;
        }

        double t_recv = vcr_now_monotonic();

        vcr_status_msg_t status;
        memset(&status, 0, sizeof(status));
        status.magic   = VCR_MAGIC;
        status.version = VCR_PROTO_VERSION;
        status.seq     = cmd.seq;
        status.t_send_echo = cmd.t_send;
        status.t_core_recv = t_recv;
        status.robot_connected = (robot != NULL);
        status.error_code = VCR_ERR_NONE;

        if (cmd.type == VCR_CMD_SHUTDOWN) {
            status.type = VCR_STATUS_ACK;
            status.ok = 1;
            ipc_server_reply(srv, ctx, &status);
            fprintf(stderr, "[control_core] shutdown requested by client\n");
            break;
        }

        if (cmd.type == VCR_CMD_PING) {
            status.type = VCR_STATUS_ACK;
            status.ok = 1;
            ipc_server_reply(srv, ctx, &status);
            continue;
        }

        if (cmd.type != VCR_CMD_MOTION) {
            status.type = VCR_STATUS_ERROR;
            status.ok = 0;
            status.error_code = VCR_ERR_BAD_REQUEST;
            ipc_server_reply(srv, ctx, &status);
            continue;
        }

        /* --- VCR_CMD_MOTION: the actual control loop --------------------- */
        long long jitter_ns = 0;
        if (prev_motion_recv_s > 0.0) {
            jitter_ns = (long long)((t_recv - prev_motion_recv_s - opts.period_s) * 1e9);
        }
        prev_motion_recv_s = t_recv;

        int rc = 0;
        if (robot && !cmd.dry_run && cmd.enable) {
            vcr_build_urscript(&cmd, script, sizeof(script));
            rc = robot_link_send(robot, script);
        }
        double t_sent = vcr_now_monotonic();

        if (robot && opts.with_telemetry) {
            robot_telemetry_t tel;
            int got = robot_link_try_read_telemetry(robot, opts.telemetry_timeout_ms, &tel);
            if (got == 1) {
                status.telemetry_valid = 1;
                memcpy(status.q_actual, tel.q_actual, sizeof(tel.q_actual));
                status.robot_mode  = tel.robot_mode;
                status.safety_mode = tel.safety_mode;
            }
        }

        status.type = VCR_STATUS_ACK;
        status.t_robot_sent = t_sent;
        status.ok = (rc == 0);
        status.error_code = (rc == 0) ? VCR_ERR_NONE : VCR_ERR_SEND_FAILED;
        status.loop_jitter_ns = jitter_ns;

        double rtt_proxy = t_sent - cmd.t_send;   /* vision-send -> robot-write */
        latency_log_write(log, cmd.seq, cmd.t_send, t_recv, t_sent, rtt_proxy, jitter_ns);

        ipc_server_reply(srv, ctx, &status);
    }

    latency_log_print_summary(log, "control_core");
    latency_log_close(log);
    if (robot) robot_link_close(robot);
    ipc_server_close(srv);
    return 0;
}
