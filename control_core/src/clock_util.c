#define _POSIX_C_SOURCE 200809L
#include "clock_util.h"
#include <time.h>

long long vcr_now_monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

double vcr_now_monotonic(void) {
    return (double)vcr_now_monotonic_ns() / 1e9;
}
