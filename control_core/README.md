# control_core — portable control core (Linux today, QNX SDP 8.0 target)

`control_core` is the **second component** of `visual_control_robot`: a small C
process with no external dependencies that is the **only** process opening the TCP
socket to the UR robot (port 30003). The vision component (Python + MediaPipe)
never talks to the robot; it sends motion commands to this process over a fixed
binary request/response protocol.

## Why it is separate

Gesture recognition does not need hard real time: it can run on general-purpose
Linux with all the variability of a webcam and MediaPipe. The loop that receives a
joint target and writes it to the robot socket does need determinism. That loop is
this tree.

The boundary between both sides is isolated in `include/ipc_transport.h` and has two
interchangeable backends implementing exactly the same contract:

- `src/ipc_posix.c`: UNIX socket, for generic Linux (default target).
- `src/ipc_qnx.c`: native QNX Neutrino message passing
  (`name_attach`/`MsgReceive`/`MsgReply` on the server,
  `name_open`/`MsgSend` on the client), for the `qnx` target.

Neither `protocol.h`, nor `robot_link.c`, nor `main.c`, nor
`tools/bench_client.c` changes between backends. The detailed plan is in
[`docs/qnx_port_plan.md`](docs/qnx_port_plan.md).

```
[vision, Python, Linux]  --vcr_cmd_msg_t-->  [control_core, C]
                         <--vcr_status_msg_t--  │
                                                 └─ TCP 30003 → UR20
```

## Build and tests

```bash
cd control_core
make clean && make
make test
```

`make` (POSIX, host `cc`) produces:

| Binary | Purpose |
|---|---|
| `build/control_core` | the server process |
| `build/bench_client` | synthetic latency/jitter harness |
| `build/test_protocol_layout` | pins the struct `sizeof` at 108 bytes |
| `build/test_telemetry_parse` | validates the type-64 stream offsets |

`make test` also imports `python_client/control_client.py`; its asserts check that
the Python `struct` layout matches `include/protocol.h` byte for byte.

### QNX SDP 8.0 cross-build

With the SDK installed and `qnxsdp-env.sh` **sourced**:

```bash
make qnx                          # ARM64 by default (aarch64le)
make qnx QNX_ARCH=x86_64          # x86_64
```

`make qnx` compiles with `qcc` into `build/qnx/`, uses `src/ipc_qnx.c` instead of
`src/ipc_posix.c` and produces `build/qnx/control_core.qnx` and
`build/qnx/bench_client.qnx`. The endpoint is normalized to a flat QNX name under
`/dev/name/local/`: leading slashes are removed and internal slashes become `_`
(`/tmp/vcr_control.sock` → `tmp_vcr_control.sock`).

## Running

Without a robot (only the loop + IPC):

```bash
./build/control_core --dry-run --endpoint /tmp/vcr_control.sock --log core.csv
```

With a real robot or a substitute:

```bash
./build/control_core --robot-ip "$ROBOT_IP" \
    --endpoint /tmp/vcr_control.sock [--with-telemetry] [--log core.csv]
```

`--with-telemetry` makes a *best-effort* read of the type-64 stream after each
command (with a bounded timeout) to fill `q_actual`/`robot_mode` in the reply. It is
off by default because it adds latency to the critical path; it is only needed for the
*home probe* and to show the real robot state in the overlay.

Then, in another terminal, run the upstream vision client:

```bash
python scripts/test_robot_control.py
```

The script reads `CONTROL_CORE_ENDPOINT` (default `/tmp/vcr_control.sock`). If it
cannot connect, it aborts with a message explaining how to start `control_core`; if
`control_core` has no robot, the script enters dry-run mode.

## Protocol

`include/protocol.h` is the source of truth. Summary:

- `vcr_cmd_msg_t` (108 B): `magic`, `version`, `type`, `seq`, `t_send`,
  `mode` (`movej`/`servoj`/`home_probe`), flags (`enable`, `dry_run`),
  `joints[6]`, `velocity`, `acceleration`, `servoj` parameters, `gain`.
- `vcr_status_msg_t` (108 B): echo of `seq`/`t_send`, timestamps
  (`t_core_recv`, `t_robot_sent`), flags (`robot_connected`, `ok`,
  `telemetry_valid`), `error_code`, `q_actual[6]`, `robot_mode`,
  `safety_mode`, `loop_jitter_ns`.

Everything is `packed`, little-endian and fixed-size: no variable framing and no
streaming. It is deliberately shaped like `MsgSend()` (one request → one blocking
reply), not like a generic RPC.

## Linux vs QNX measurement

```bash
# Terminal 1
./build/control_core --dry-run --endpoint /tmp/vcr_control.sock

# Terminal 2
./build/bench_client --endpoint /tmp/vcr_control.sock \
    --rate-hz 20 --count 2000 --log bench_linux.csv
```

`bench_client` and `control_core` write one CSV row per sample and a summary
`n/min/mean/max/stddev`. The `stddev` is the jitter proxy and the p99 RTT is the
figure to compare. If you change `--rate-hz`, pass the same period to
`control_core --period-s` (20 Hz → 0.05): jitter is the deviation from that
reference, not the raw interval. The same command, without recompiling the client, is
run against `control_core.qnx` on the QNX target to produce the comparison plot in
the demo.

## QNX port status

`src/ipc_qnx.c` and the `make qnx` target are **implemented**. The QNX backend has
been cross-compiled with `qcc` and executed inside QNX 8.0 in QEMU (QSTI, x86_64,
KVM); the benchmark is measured and saved in `results/qnx_bench.csv` and
`results/qnx_core.csv`. See [`docs/qnx_port_plan.md`](docs/qnx_port_plan.md) for
the function-by-function mapping and the QEMU caveat.

The type-64 telemetry parser is pinned to the deterministic mock; the real URSim
type-64 stream is big-endian with different offsets, so `--with-telemetry` is only
verified against the mock in this repository.
