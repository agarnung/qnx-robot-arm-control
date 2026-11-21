# Vision replay (optional PoC)

This directory is a small, deterministic PoC of the **vision -> control_core**
path. It does **not** run MediaPipe, a webcam or any vision model on QNX. It
replays a sequence of joint targets (the output of the upstream vision pipeline)
through the same 108-byte IPC protocol that the real vision process uses.

The point is to exercise the full path without a camera:

```
recorded joint targets -> ControlCoreClient -> control_core -> TCP 30003 -> UR20/URSim
```

## Run against a dry-run core

```bash
cd control_core && make
./build/control_core --dry-run --endpoint /tmp/vcr_replay.sock
```

In another terminal:

```bash
python3 examples/vision_replay/replay.py \
    --endpoint /tmp/vcr_replay.sock \
    --json examples/vision_replay/session.json \
    --mode servoj --rate-hz 20
```

## Run against URSim

Start `control_core` with `--robot-ip 127.0.0.1` (see the main README and
`qnx-emulator/ursim.sh`), then use the same replay command without
`--dry-run`.

## Format

```json
{
  "keyframes": [
    {"t": 0.0, "joints": [0.0, -1.2, 0.9, -1.4, 0.4, 0.1]},
    {"t": 1.5, "joints": [0.55, -1.05, 1.15, -1.2, 0.65, 0.1]}
  ]
}
```

`session.json` is a **synthetic fixture** for interface testing. Replace it with a
sequence recorded from the upstream vision pipeline to replay a real session. The
replay is intentionally not part of the QNX build; it is a host-side client, just
like the Python vision process.

## What this proves

- The 108-byte command/status protocol works from a real Python client.
- `control_core` is the only owner of the robot socket.
- A recorded vision output can drive the QNX control core without changes.

## What this does not prove

- It does not run MediaPipe or a camera on QNX.
- It does not measure vision latency.
- The example fixture is not a recording of a human; it only tests the interface.
