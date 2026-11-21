#include "latency_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

struct latency_log {
    FILE *fp;
    long   count;
    double min_s, max_s, sum_s, sum_sq_s;
};

latency_log_t *latency_log_open(const char *csv_path) {
    latency_log_t *log = calloc(1, sizeof(*log));
    if (!log) return NULL;
    log->min_s = 1e300;
    log->max_s = -1e300;

    if (csv_path) {
        log->fp = fopen(csv_path, "w");
        if (log->fp) {
            fprintf(log->fp,
                "seq,t_send,t_core_recv,t_robot_sent,"
                "ipc_latency_s,dispatch_latency_s,metric_s,jitter_ns\n");
        }
    }
    return log;
}

void latency_log_write(latency_log_t *log, unsigned seq,
                        double t_send, double t_core_recv, double t_robot_sent,
                        double metric_s, long long jitter_ns) {
    if (!log) return;

    double ipc_latency      = t_core_recv - t_send;
    double dispatch_latency = (t_robot_sent > 0.0) ? (t_robot_sent - t_core_recv) : 0.0;

    if (log->fp) {
        fprintf(log->fp, "%u,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%lld\n",
                seq, t_send, t_core_recv, t_robot_sent,
                ipc_latency, dispatch_latency, metric_s, jitter_ns);
        fflush(log->fp);   /* flush per sample: this is a diagnostics tool,
                               not the hot path — correctness over throughput */
    }

    log->count++;
    if (metric_s < log->min_s) log->min_s = metric_s;
    if (metric_s > log->max_s) log->max_s = metric_s;
    log->sum_s    += metric_s;
    log->sum_sq_s += metric_s * metric_s;
}

latency_stats_t latency_log_stats(const latency_log_t *log) {
    latency_stats_t s = {0};
    if (!log || log->count == 0) return s;

    s.count = log->count;
    s.min_s = log->min_s;
    s.max_s = log->max_s;
    s.mean_s = log->sum_s / (double)log->count;

    double variance = (log->sum_sq_s / (double)log->count) - (s.mean_s * s.mean_s);
    if (variance < 0.0) variance = 0.0;   /* guard against fp rounding */
    s.stddev_s = sqrt(variance);
    return s;
}

void latency_log_print_summary(const latency_log_t *log, const char *label) {
    latency_stats_t s = latency_log_stats(log);
    if (s.count == 0) {
        fprintf(stderr, "[%s] no samples recorded\n", label ? label : "latency");
        return;
    }
    fprintf(stderr,
        "[%s] n=%ld  min=%.3fms  mean=%.3fms  max=%.3fms  stddev(jitter)=%.3fms\n",
        label ? label : "latency", s.count,
        s.min_s * 1e3, s.mean_s * 1e3, s.max_s * 1e3, s.stddev_s * 1e3);
}

void latency_log_close(latency_log_t *log) {
    if (!log) return;
    if (log->fp) fclose(log->fp);
    free(log);
}
