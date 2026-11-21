/*
 * bench_client.c — synthetic latency/jitter harness.
 *
 * Connects to a running control_core instance exactly like the real vision
 * process does (same ipc_transport.h client API), but instead of real
 * MediaPipe joint targets it sends a synthetic sinusoidal trajectory at a
 * fixed rate. This isolates the number every QNX judge will ask for —
 * "what is the latency and jitter of the control loop itself" — from
 * webcam/MediaPipe frame-time noise, and is exactly what gets re-run
 * unmodified against a QNX build of control_core to produce the
 * Linux-vs-QNX comparison the hackathon pitch is built on.
 *
 * Usage:
 *   bench_client --endpoint /tmp/vcr_control.sock --rate-hz 10 --count 500
 *               [--log bench.csv]
 *
 * Run control_core with --dry-run (no real robot needed) or against a real
 * robot/QEMU target once available — same binary either way.
 */
#define _POSIX_C_SOURCE 200809L
#include "ipc_transport.h"
#include "clock_util.h"
#include "latency_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

static void sleep_s(double s) {
    if (s <= 0) return;
    struct timespec ts;
    ts.tv_sec  = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    const char *endpoint = "/tmp/vcr_control.sock";
    const char *log_path = NULL;
    double rate_hz = 10.0;
    long   count = 500;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--endpoint") && i + 1 < argc) endpoint = argv[++i];
        else if (!strcmp(argv[i], "--rate-hz") && i + 1 < argc) rate_hz = atof(argv[++i]);
        else if (!strcmp(argv[i], "--count") && i + 1 < argc) count = atol(argv[++i]);
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) log_path = argv[++i];
        else {
            fprintf(stderr, "usage: %s [--endpoint PATH] [--rate-hz N] "
                             "[--count N] [--log PATH]\n", argv[0]);
            return 2;
        }
    }

    signal(SIGINT, on_sigint);

    ipc_client_t *cli = ipc_client_connect(endpoint);
    if (!cli) {
        perror("ipc_client_connect (is control_core running?)");
        return 1;
    }

    latency_log_t *log = latency_log_open(log_path);
    double period = 1.0 / rate_hz;

    fprintf(stderr, "[bench_client] sending %ld synthetic commands at %.1f Hz "
                     "to %s\n", count, rate_hz, endpoint);

    double q_home[6] = {0, -1.578, 0, 0, 0, 0};   /* ~ -90.4 deg shoulder, like .env default */

    for (long i = 0; i < count && !g_stop; i++) {
        vcr_cmd_msg_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.magic = VCR_MAGIC;
        cmd.version = VCR_PROTO_VERSION;
        cmd.type = VCR_CMD_MOTION;
        cmd.seq = (uint32_t)i;
        cmd.mode = VCR_MODE_MOVEJ;
        cmd.enable = 1;
        cmd.dry_run = 0;   /* control_core's own --dry-run flag governs robot I/O */

        double phase = 2.0 * M_PI * (double)i / 50.0;
        for (int j = 0; j < 6; j++) cmd.joints[j] = q_home[j];
        cmd.joints[0] += 0.05 * sin(phase);   /* small synthetic base sweep */

        cmd.velocity = 0.3;
        cmd.acceleration = 0.5;
        cmd.servoj_t = period * 2;
        cmd.servoj_lookahead = 0.1;
        cmd.servoj_gain = 300;

        cmd.t_send = vcr_now_monotonic();

        vcr_status_msg_t status;
        if (ipc_client_send_request(cli, &cmd, &status) != 0) {
            perror("ipc_client_send_request");
            break;
        }

        double t_reply = vcr_now_monotonic();
        double rtt = t_reply - cmd.t_send;
        latency_log_write(log, cmd.seq, cmd.t_send, status.t_core_recv,
                           status.t_robot_sent, rtt, status.loop_jitter_ns);

        sleep_s(period);
    }

    latency_log_print_summary(log, "bench_client (end-to-end RTT)");
    latency_log_close(log);
    ipc_client_disconnect(cli);
    return 0;
}
