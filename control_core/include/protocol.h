/*
 * protocol.h — Wire format for the vision <-> control-core IPC link.
 *
 * This is the "well-defined command/status interface" between the two
 * decoupled components of visual_control_robot:
 *
 *   [vision process, Python, generic Linux]  --cmd_msg-->  [control_core, C]
 *   [vision process, Python, generic Linux]  <--status_msg-- [control_core, C]
 *
 * The protocol is a fixed-size binary request/reply pair, deliberately
 * modelled after QNX Neutrino's synchronous message-passing primitive
 * (MsgSend()/MsgReceive()/MsgReply()) rather than a generic RPC framework:
 * one request, one blocking reply, no streaming, no framing beyond "read
 * exactly sizeof(struct) bytes". See docs/qnx_port_plan.md for the mapping.
 *
 * IMPORTANT: every struct here is declared with __attribute__((packed)) and
 * uses only fixed-width types so that:
 *   (a) the C layout is 100% portable across the machines this currently
 *       targets (x86_64 / ARM64 Linux, little-endian), and
 *   (b) python_client/control_client.py can pack/unpack the exact same
 *       bytes with the stdlib `struct` module (format strings are given
 *       in comments next to each struct).
 *
 * Bumping VCR_PROTO_VERSION is mandatory on any layout change.
 */
#ifndef VCR_PROTOCOL_H
#define VCR_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VCR_MAGIC          0x56435243u   /* ASCII 'VCRC' */
#define VCR_PROTO_VERSION  1

/* cmd_msg.type */
enum {
    VCR_CMD_MOTION   = 1,   /* move the arm to cmd.joints                */
    VCR_CMD_PING     = 2,   /* liveness / latency probe, no robot I/O    */
    VCR_CMD_SHUTDOWN = 3,   /* ask control_core to exit cleanly          */
};

/* cmd_msg.mode */
enum {
    VCR_MODE_MOVEJ = 0,       /* point-to-point, robot stops between cmds  */
    VCR_MODE_SERVOJ = 1,      /* continuous interpolation, no stop-go      */
    VCR_MODE_HOME_PROBE = 2,  /* cmd.joints IGNORED: emits
                                 servoj(get_joint_positions(), ...) so the
                                 *controller* reports its own actual
                                 position while holding still. Needed
                                 because Polyscope X only populates
                                 q_actual on the 30003 stream while a motion
                                 command is RUNNING (see
                                 src/robot/telemetry.py::read_actual_position
                                 for the original, pre-split rationale).
                                 Requires control_core started with
                                 --with-telemetry to be useful. */
};

/* status_msg.type */
enum {
    VCR_STATUS_ACK   = 1,
    VCR_STATUS_ERROR = 2,
};

/* status_msg.error_code */
enum {
    VCR_ERR_NONE            = 0,
    VCR_ERR_ROBOT_UNREACHABLE = 1,
    VCR_ERR_SEND_FAILED     = 2,
    VCR_ERR_BAD_REQUEST     = 3,
};

/*
 * vcr_cmd_msg_t — vision -> control_core
 *
 * Python struct format (little-endian, no implicit padding):
 *   "<IHHIdBBBBddddddddddI"
 * (magic, version, type, seq, t_send,
 *  mode, enable, dry_run, _pad,
 *  joints[0..5] (6x d), velocity, acceleration,
 *  servoj_t, servoj_lookahead  [10 doubles total], servoj_gain)
 * sizeof == 108 bytes; pinned by python_client/control_client.py and by
 * control_core/tests (CMD_STRUCT_SIZE).
 *
 * Field order is chosen so the natural C alignment already matches the
 * packed layout on x86_64/ARM64 (8-byte doubles after the 4-byte header
 * fields land on 8-byte boundaries); `packed` is still declared to make
 * this an explicit contract rather than an accident of the toolchain.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;             /* VCR_MAGIC                              */
    uint16_t version;           /* VCR_PROTO_VERSION                      */
    uint16_t type;              /* VCR_CMD_*                               */
    uint32_t seq;               /* monotonically increasing per vision run */
    double   t_send;            /* CLOCK_MONOTONIC seconds, set by vision  */

    uint8_t  mode;              /* VCR_MODE_*                              */
    uint8_t  enable;            /* 0 = compute but do not actuate robot    */
    uint8_t  dry_run;           /* 1 = never touch the socket to the robot */
    uint8_t  _pad0;

    double   joints[6];         /* target joint positions, rad             */
    double   velocity;          /* rad/s                                   */
    double   acceleration;      /* rad/s^2                                 */

    double   servoj_t;          /* servoj only: segment time, s            */
    double   servoj_lookahead;  /* servoj only: lookahead_time, s          */
    uint32_t servoj_gain;       /* servoj only: gain                       */
} vcr_cmd_msg_t;

/*
 * vcr_status_msg_t — control_core -> vision (reply)
 *
 * Python struct format:
 *   "<IHHIdddBBBBiddddddiiq"
 * (magic, version, type, seq, t_send_echo, t_core_recv, t_robot_sent,
 *  robot_connected, ok, telemetry_valid, _pad, error_code,
 *  q_actual[0..5] (6x d), robot_mode, safety_mode, loop_jitter_ns)
 * sizeof == 108 bytes.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t type;               /* VCR_STATUS_*                          */
    uint32_t seq;                 /* echo of the request's seq             */

    double   t_send_echo;         /* echo of cmd.t_send (for RTT calc)     */
    double   t_core_recv;         /* when control_core dequeued the request*/
    double   t_robot_sent;        /* when the URScript left the core's     *
                                    * socket toward the robot (0 if n/a)    */

    uint8_t  robot_connected;     /* control_core has a live robot socket  */
    uint8_t  ok;                  /* request handled without error         */
    uint8_t  telemetry_valid;     /* q_actual/robot_mode/safety_mode below *
                                    * were read back from the robot in this*
                                    * same request (best-effort, see        *
                                    * robot_link.h)                         */
    uint8_t  _pad0;
    int32_t  error_code;          /* VCR_ERR_*                              */

    double   q_actual[6];         /* rad, only meaningful if telemetry_valid*/
    int32_t  robot_mode;
    int32_t  safety_mode;

    int64_t  loop_jitter_ns;      /* core-side: time since the previous    *
                                    * VCR_CMD_MOTION request arrived, minus *
                                    * the nominal period (0 if first cmd)   *
                                    * — signed, can be negative             */
} vcr_status_msg_t;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/* Fail the build, not a runtime EPROTO, if the frozen wire layout drifts. */
_Static_assert(sizeof(vcr_cmd_msg_t) == 108, "vcr_cmd_msg_t must stay 108 bytes");
_Static_assert(sizeof(vcr_status_msg_t) == 108, "vcr_status_msg_t must stay 108 bytes");
#endif

#ifdef __cplusplus
}
#endif

#endif /* VCR_PROTOCOL_H */
