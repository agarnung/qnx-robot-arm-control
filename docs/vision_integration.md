# Vision integration and parallel work

This repository is the **QNX control half** of `visual_control_robot`. The vision
half (Python + MediaPipe + webcam) is upstream and intentionally not ported to QNX.
This note records the boundary and a small replay PoC so the boundary is testable.

## Boundary

```
upstream vision (Linux, soft real-time)
  webcam -> MediaPipe -> gesture -> joint target
        |
        |  vcr_cmd_msg_t (108 bytes), POSIX socket or QNX MsgSend
        v
control_core (this repo, hard real-time)
        |
        |  TCP 30003, URScript
        v
UR20 / URSim
```

- The vision side may drop frames; a dropped frame is harmless.
- The control side owns the robot socket and must not miss deadlines.
- `protocol.h`, `robot_link.c`, `main.c` and `bench_client.c` do not change
  between the POSIX and QNX backends; only `ipc_*.c` changes.

## Replay PoC

[`examples/vision_replay/`](../examples/vision_replay/) replays a recorded
joint-target sequence through `ControlCoreClient` and the same 108-byte protocol.
It is a host-side client, not part of the QNX binary, and it does not run
MediaPipe or open a camera. It is useful for:

- testing the full path without a webcam;
- reproducing a session deterministically;
- demonstrating that the QNX core is the only owner of the robot socket.

The included `session.json` is a **synthetic fixture** for interface testing.
Replace it with a sequence recorded from the upstream vision pipeline to replay a
real session.

## Parallel work

1. Record hand landmarks and gesture labels from the upstream vision pipeline.
2. Add a deterministic replay of those landmarks (not just joint targets) to test
   gesture-to-target mapping.
3. Measure vision latency separately from control latency.
4. Keep MediaPipe on Linux; do not port it to QNX. The split is the point: the
   soft-real-time producer stays on a general-purpose OS, the hard-real-time
   consumer runs on QNX.
5. If a camera is added to a QNX board later, it should be a separate producer
   process, not a second owner of the robot socket.

## Non-goals

- Porting MediaPipe to QNX.
- Running a camera or a vision model inside `control_core`.
- Claiming production jitter from QEMU. QEMU validates the port and the message
  path, not a hardware timing figure.
