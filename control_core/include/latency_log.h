/*
 * latency_log.h — CSV logging + running stats for the latency/jitter
 * harness. The whole point of pulling the control loop into its own C
 * module ahead of a QNX port is to be able to answer, with numbers:
 *
 *   "how much lower and how much more deterministic is the command-to-
 *    socket-write latency on QNX Neutrino than on generic Linux?"
 *
 * This module writes one CSV row per VCR_CMD_MOTION request (seq,
 * t_send, t_core_recv, t_robot_sent, ipc_latency_s, dispatch_latency_s,
 * jitter_ns) and keeps running min/max/mean/stddev so a human-readable
 * summary can be printed when control_core or bench_client exits —
 * exactly the artifact to diff between a Linux run and a QNX run.
 */
#ifndef VCR_LATENCY_LOG_H
#define VCR_LATENCY_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct latency_log latency_log_t;

typedef struct {
    long   count;
    double min_s, max_s, mean_s;
    double stddev_s;         /* of the sampled metric (e.g. RTT), proxy for jitter */
} latency_stats_t;

/* path == NULL disables the CSV file (stats are still accumulated). */
latency_log_t *latency_log_open(const char *csv_path);

/* One row per measurement. `jitter_ns` is caller-defined (see protocol.h's
 * loop_jitter_ns: core-side inter-arrival jitter) and purely informational
 * in the CSV; `metric_s` (typically round-trip time) is what feeds the
 * running stats returned by latency_log_stats(). */
void latency_log_write(latency_log_t *log, unsigned seq,
                        double t_send, double t_core_recv, double t_robot_sent,
                        double metric_s, long long jitter_ns);

latency_stats_t latency_log_stats(const latency_log_t *log);

void latency_log_print_summary(const latency_log_t *log, const char *label);

void latency_log_close(latency_log_t *log);

#ifdef __cplusplus
}
#endif

#endif /* VCR_LATENCY_LOG_H */
