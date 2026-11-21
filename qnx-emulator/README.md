# QNX in QEMU — demo without hardware

This directory lets you **build, boot and measure `control_core` on QNX SDP 8.0
inside QEMU**, without buying a board or a robot. The robot is replaced by a
**simulated UR server** (`mock_ur_server.py`) that speaks the same TCP 30003
socket and emits type-64 telemetry.

```
┌──────────────────────────── Linux host ───────────────────────────┐
│  mock_ur_server.py :30003  (or real URSim, optional)            │
│                                                                 │
│  ┌──────────────── QEMU (qemu-system-x86_64) ────────────────┐  │
│  │  QNX SDP 8.0 Neutrino                                    │  │
│  │    control_core.qnx  ── MsgSend/MsgReceive (native IPC)   │  │
│  │         │                                                 │  │
│  │         └── TCP socket 10.0.2.2:30003 ───────────────────┼──┘
│  └───────────────────────────────────────────────────────────┘
└─────────────────────────────────────────────────────────────────┘
```

## Requirements

- QEMU (`qemu-system-x86_64`; on Debian/Ubuntu: `qemu-system-x86`).
- myQNX account + **free non-commercial QNX SDP 8.0 license**.
- `qcc` (included with the SDP) for the cross-build.
- Python 3 on the host for the simulated UR server.
- **`com.qnx.qnx800.quickstart.qemu`** installed from QNX Software Center and
  unpacked with `unpack_qemu_image.sh`
  (`~/qnx800/images/qemu/qemu/output/{ifs.bin,disk-qemu.vmdk}`).

Exact steps: [`download-qnx.md`](download-qnx.md).

## Full flow

```bash
# 0) validate the harness without QNX (mock UR + POSIX control_core + bench)
./test-harness-linux.sh

# 1) cross-compile for QNX (x86_64 for QEMU x86_64)
QNX_ENV=~/qnx800/qnxsdp-env.sh QNX_ARCH=x86_64 ./build-control-core.sh

# 2) boot QNX in QEMU (QSTI under ~/qnx800/images/qemu/qemu/output)
QNX_SHARE_IF=ide ./run-qnx.sh
#    QNX_GUI=1 ./run-qnx.sh   # QNX desktop (SDL/GL)

# 3) inside QNX: mount the shared disk and measure
#    (the QSTI system disk is hd0; the shared FAT is usually hd1)
ls /dev/hd*
mkdir -p /data/share
mount -t dos /dev/hd1t6 /data/share
cd /data/share && sh bench-inside-qnx.sh

# 4) on the host, simulated UR server (optional but recommended)
python3 mock_ur_server.py --host 0.0.0.0 --port 30003
```

> In this QSTI the shared FAT appears as `/dev/hd1t6`; adjust it according to
> `ls /dev/hd*`. `/mnt` does not exist in the QSTI: use `/data/share`.
> The FAT does not allow executing binaries: copy the `.qnx` files to `/tmp` and
> run them from there.

## Scripts

| Script | Runs on | What it does |
|---|---|---|
| `test-harness-linux.sh` | host | Tests mock UR + POSIX `control_core` + `bench_client` |
| `build-control-core.sh` | host | `make qnx` with `qcc`, leaves binaries in `dist/` |
| `run-qnx.sh` | host | Boots QNX in QEMU with network and shared disk |
| `bench-inside-qnx.sh` | QNX VM | `control_core.qnx` + `bench_client.qnx` → CSV |
| `mock_ur_server.py` | host | UR double on :30003 + type-64 telemetry |
| `ursim.sh` | host | URSim UR20 in Docker (VNC + TCP 30003) |

## What it demonstrates

1. `control_core` compiles with `qcc` for QNX without changing `protocol.h`,
   `robot_link.c`, `main.c` or `bench_client.c`.
2. The `src/ipc_qnx.c` backend (native message passing) answers the same
   108-byte protocol as the POSIX backend.
3. The control loop talks to a (simulated) robot over TCP 30003 and fills
   `q_actual`/`robot_mode` with type-64 telemetry.
4. A Linux vs QNX comparison of RTT/jitter **in QEMU**.

## Honest limitation

QEMU emulates the CPU and devices, but it is **not an RTOS on real hardware**:
the jitter measured in QEMU is not the jitter of a physical QNX board. The
comparison validates the port and the architecture; for a "production" figure it
must be repeated on a supported board (Raspberry Pi 4/5 with a QNX BSP, an x86_64
mini-PC, or the judges' hardware). The QEMU demo is still valid for building
and recording the video without buying anything.

## Alternative if the catalog does not offer a QEMU target

- Use the generic `x86_64` BSP and boot the IFS with
  `QNX_KERNEL=/path/qnx.ifs ./run-qnx.sh`.
- Or install QNX in VMware/VirtualBox; the binaries and scripts are the same.
- In the worst case, `test-harness-linux.sh` already demonstrates the full loop
  on POSIX and `build-control-core.sh` leaves the binary ready for when the image
  is available.
