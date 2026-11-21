/*
 * robot_link.h — outbound TCP client to the UR Real-Time Client Interface
 * (port 30003), the same interface src/robot/connection.py (UR30003) talks
 * to from Python today. This is a plain TCP client on EVERY target,
 * including a QNX build: it is not the IPC abstraction point (see
 * ipc_transport.h for that) because the robot's network interface is fixed
 * by Universal Robots and is unrelated to which OS runs the control core.
 *
 * Deliberately minimal: control_core's job is to dispatch the next joint
 * target with the lowest, most predictable latency, not to reimplement the
 * full telemetry/dashboard feature set already covered by the Python side
 * for diagnostics (src/robot/telemetry.py, src/robot/dashboard.py).
 * Reading back q_actual is therefore best-effort and OFF by default — see
 * robot_link_try_read_telemetry() below.
 */
#ifndef VCR_ROBOT_LINK_H
#define VCR_ROBOT_LINK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robot_link robot_link_t;

typedef struct {
    double  q_actual[6];   /* rad */
    int32_t robot_mode;
    int32_t safety_mode;
} robot_telemetry_t;

/* Connect to ip:30003. Returns NULL on failure (errno set). */
robot_link_t *robot_link_connect(const char *ip);

/* Send a URScript line (newline appended if missing). Returns 0 on success,
 * -1 on error. */
int robot_link_send(robot_link_t *link, const char *urscript);

/*
 * Best-effort: try to read and parse one type-64 flat telemetry packet
 * (Polyscope X RT stream, little-endian, see docs/telemetry_format.md for
 * the field layout this mirrors from src/robot/telemetry.py) within
 * `timeout_ms`. Returns 1 if a packet was parsed into *out, 0 on timeout
 * (no data ready — NOT an error, the 125 Hz stream may simply not have
 * delivered a fresh frame yet within the budget), -1 on a real socket
 * error.
 *
 * Deliberately bounded by timeout_ms so a caller on the critical dispatch
 * path (control_loop.c) can cap the worst-case added latency — see the
 * `--with-telemetry` flag in main.c and the discussion in
 * docs/qnx_port_plan.md on why this timeout is itself a symptom of the
 * problem QNX message passing + a dedicated thread/priority would remove.
 */
int robot_link_try_read_telemetry(robot_link_t *link, int timeout_ms,
                                   robot_telemetry_t *out);

void robot_link_close(robot_link_t *link);

/*
 * Exposed only so tests/test_telemetry_parse.c can pin the byte-offset
 * parsing against a synthetic packet without opening a real socket. Not
 * part of the public API consumed by control_loop.c.
 */
int vcr_parse_type64_telemetry(const uint8_t *payload, size_t len,
                                robot_telemetry_t *out);

#ifdef __cplusplus
}
#endif

#endif /* VCR_ROBOT_LINK_H */
