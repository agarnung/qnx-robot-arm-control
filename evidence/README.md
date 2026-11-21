# Evidence — real sessions and clips for the video

Everything in this folder was produced by running the commands for real on this
machine (Linux host + QNX 8.0 QSTI in QEMU x86_64 with KVM). No figures or
screens were invented.

## What is persisted

| File | What it proves |
|---|---|
| `linux-baseline.log` | Real transcript of `make`, `make test`, `make sanitize`, `make analyze`, the Linux benchmark and `compare_results.py`, with `qcc 12.2.0` and `file` on the QNX binary. |
| `qnx-session.log` | Real transcript of the QNX boot, login, `uname -a`, `pidin info`, `ls /dev/hd*`, mounting the shared FAT and the benchmark with `control_core.qnx` + `bench_client.qnx`. |
| `linux_bench_replay.csv` / `linux_core_replay.csv` | Replay of 500 samples from the Linux benchmark clip. |
| `clips/*.mp4` | 1280x720 (H.264) terminal clips generated from the two logs. These are local and not committed (see `.gitignore`). |
| `../results/qnx_bench.csv` / `../results/qnx_core.csv` | Canonical QNX measurement: 2000 samples at 20 Hz. |
| `../results/linux_bench.csv` / `../results/linux_core.csv` | Canonical Linux measurement: 500 samples at 20 Hz. |
| `../results/README.md` | Linux vs QNX table and caveats (QEMU outliers, not real hardware). |

## Clips

| Clip | Content | Duration | Suggested use |
|---|---|---:|---|
| `01-linux-build.mp4` | `qcc`/`file` on the QNX ELF + Linux build | 30.8 s | Speed up 2x or use 10 s |
| `02-linux-tests.mp4` | `make test` (layout, telemetry, URScript, Python client) | 15.1 s | Full |
| `03-linux-hardening.mp4` | ASan/UBSan + `gcc -fanalyzer` | 42.7 s | Speed up 4x or only the end |
| `04-linux-bench.mp4` | Linux benchmark: 500 commands at 20 Hz | 29.0 s | Speed up 2x |
| `05-linux-compare.mp4` | Linux vs QNX table | 9.8 s | Full |
| `06-qnx-boot.mp4` | Last lines of the QNX 8.0 boot | 2.0 s | Full |
| `07-qnx-login.mp4` | `root`/`root` login + QNX motd | 16.5 s | Trim to 5–6 s |
| `08-qnx-version.mp4` | `uname -a`, `pidin info`, `ls /dev/hd*` | 13.2 s | Full |
| `09-qnx-mount.mp4` | Mount the shared FAT and copy to `/tmp` | 14.6 s | Speed up 2x |
| `10-qnx-bench.mp4` | QNX benchmark (recorded session) | 32.3 s | Speed up 2x |
| `11-qnx-csv.mp4` | `bench_qnx.csv` + `core_qnx.csv` | 5.2 s | Full |

The clips are **animated transcripts** of the real captured output, not a
real-time screen recording: the video says so (or shows it as "real session
output"). The benchmark wait time is compressed; the full 2000-sample measurement
is in `results/`.

## Suggested order for a video < 3 min

1. `06-qnx-boot` + `07-qnx-login` (trimmed) — 8 s
2. `01-linux-build` sped up — 15 s
3. `02-linux-tests` — 15 s
4. `08-qnx-version` — 13 s
5. `09-qnx-mount` sped up — 7 s
6. `10-qnx-bench` sped up — 16 s
7. `04-linux-bench` sped up — 15 s
8. `05-linux-compare` — 10 s
9. `11-qnx-csv` — 5 s

Total ≈ 1:44, leaving room for narration, architecture and closing.

## Reproduce

```bash
./evidence/record-linux-baseline.sh
./evidence/record-qnx-session.py
./evidence/render-clips.py
```

`record-qnx-session.py` boots QEMU with `QNX_SHARE_IF=ide`, logs in, mounts
`/dev/hd1t6` at `/data/share`, copies the binaries to `/tmp/share` and runs the
benchmark.
