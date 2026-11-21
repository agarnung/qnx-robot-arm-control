/*
 * test_telemetry_parse.c — pins robot_link.c's type-64 byte-offset parsing
 * against a synthetic packet, so an offset/endianness slip is caught by
 * `make test` instead of silently corrupting q_actual against a real
 * robot (which we have no access to in this environment). Not a full unit
 * test framework — just enough asserts to be a meaningful guardrail, in
 * the spirit of "verify before you trust it" for code nobody can run
 * against real hardware yet.
 */
#include "robot_link.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static void put_f64_le(uint8_t *p, double v) {
    memcpy(p, &v, 8);
}

static int almost_equal(double a, double b) {
    return fabs(a - b) < 1e-9;
}

int main(void) {
    uint8_t payload[1220];
    memset(payload, 0, sizeof(payload));
    payload[0] = 64;   /* type */

    double expected_q[6] = { 0.1, -0.2, 0.3, -0.4, 0.5, -0.6 };
    for (int i = 0; i < 6; i++) {
        put_f64_le(payload + 249 + i * 8, expected_q[i]);
    }
    put_f64_le(payload + 753, 2.0);   /* robot_mode = READY */
    put_f64_le(payload + 809, 1.0);   /* safety_mode = NORMAL */

    robot_telemetry_t tel;
    memset(&tel, 0, sizeof(tel));

    int ok = vcr_parse_type64_telemetry(payload, sizeof(payload), &tel);
    if (!ok) {
        fprintf(stderr, "FAIL: parser rejected a well-formed type-64 packet\n");
        return 1;
    }

    for (int i = 0; i < 6; i++) {
        if (!almost_equal(tel.q_actual[i], expected_q[i])) {
            fprintf(stderr, "FAIL: q_actual[%d] = %f, expected %f\n",
                    i, tel.q_actual[i], expected_q[i]);
            return 1;
        }
    }
    if (tel.robot_mode != 2) {
        fprintf(stderr, "FAIL: robot_mode = %d, expected 2 (READY)\n", tel.robot_mode);
        return 1;
    }
    if (tel.safety_mode != 1) {
        fprintf(stderr, "FAIL: safety_mode = %d, expected 1 (NORMAL)\n", tel.safety_mode);
        return 1;
    }

    /* Also check rejection: too short / wrong type byte. */
    uint8_t short_payload[10] = {64};
    robot_telemetry_t tel2;
    if (vcr_parse_type64_telemetry(short_payload, sizeof(short_payload), &tel2)) {
        fprintf(stderr, "FAIL: parser accepted a too-short payload\n");
        return 1;
    }

    uint8_t wrong_type[1220];
    memcpy(wrong_type, payload, sizeof(wrong_type));
    wrong_type[0] = 16;
    if (vcr_parse_type64_telemetry(wrong_type, sizeof(wrong_type), &tel2)) {
        fprintf(stderr, "FAIL: parser accepted a type-16 payload as type-64\n");
        return 1;
    }

    printf("OK: test_telemetry_parse (6 joints, robot_mode, safety_mode, "
           "short+wrong-type rejection)\n");
    return 0;
}
