/* clock_util.h — monotonic time helper shared by every component that needs
 * to timestamp messages for the latency/jitter measurements. Always
 * CLOCK_MONOTONIC: never affected by NTP steps or wall-clock changes, and
 * the one clock source that has a direct, documented equivalent on QNX
 * (ClockCycles() / clock_gettime(CLOCK_MONOTONIC, ...) — QNX Neutrino
 * supports the same POSIX clock_gettime() call). */
#ifndef VCR_CLOCK_UTIL_H
#define VCR_CLOCK_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Seconds, as a double, from CLOCK_MONOTONIC. Matches the `t_send` /
 * `t_core_recv` / `t_robot_sent` fields in protocol.h. */
double vcr_now_monotonic(void);

/* Same clock, nanosecond integer resolution — used for jitter arithmetic
 * where subtracting two doubles would lose precision at high tick rates. */
long long vcr_now_monotonic_ns(void);

#ifdef __cplusplus
}
#endif

#endif /* VCR_CLOCK_UTIL_H */
