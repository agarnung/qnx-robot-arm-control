#!/usr/bin/env python3
"""Compare the bench_client CSVs (Linux vs QNX) with no external dependencies.

Usage:
    python3 compare_results.py
    python3 compare_results.py results/linux_bench.csv results/qnx_bench.csv

Prints a table and, if matplotlib is available, writes results/linux_vs_qnx.png.
"""

from __future__ import annotations

import csv
import statistics as st
import sys
from pathlib import Path


def load(path: Path) -> dict:
    rows = list(csv.DictReader(path.open(encoding="utf-8")))
    rtt = [float(r["metric_s"]) * 1000.0 for r in rows]  # ms
    ipc = [float(r["ipc_latency_s"]) * 1000.0 for r in rows]
    disp = [float(r["dispatch_latency_s"]) * 1000.0 for r in rows]
    if not rtt:
        raise SystemExit(f"empty CSV: {path}")
    return {
        "name": path.stem,
        "n": len(rtt),
        "min": min(rtt),
        "mean": st.fmean(rtt),
        "median": st.median(rtt),
        "p99": sorted(rtt)[max(0, int(round(0.99 * (len(rtt) - 1))))],
        "max": max(rtt),
        "stddev": st.pstdev(rtt),
        "ipc_mean": st.fmean(ipc),
        "dispatch_mean": st.fmean(disp),
        "rtt": rtt,
    }


def show(s: dict):
    print(
        f"{s['name']:>18}  n={s['n']:5d}  "
        f"min={s['min']:.3f}  mean={s['mean']:.3f}  med={s['median']:.3f}  "
        f"p99={s['p99']:.3f}  max={s['max']:.3f}  std={s['stddev']:.3f} ms"
    )


def plot(a: dict, b: dict, out: Path):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception:
        print("[i] matplotlib not available; skipping the plot")
        return
    labels = ["mean", "median", "p99", "max", "stddev"]
    x = range(len(labels))
    va = [a[k] for k in labels]
    vb = [b[k] for k in labels]
    fig, ax = plt.subplots(figsize=(8, 4.2))
    ax.bar([i - 0.2 for i in x], va, width=0.4, label=f"{a['name']} (Linux)", color="#1f6f4a")
    ax.bar([i + 0.2 for i in x], vb, width=0.4, label=f"{b['name']} (QNX)", color="#a63d2f")
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels)
    ax.set_ylabel("ms")
    ax.set_title("Control loop latency: Linux/POSIX vs QNX/QEMU (same client, same 108 B message)")
    ax.legend(frameon=False)
    ax.grid(axis="y", alpha=.25)
    fig.tight_layout()
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=160, bbox_inches="tight")
    print(f"[ok] {out}")


def main() -> int:
    root = Path(__file__).resolve().parent
    args = sys.argv[1:]
    linux = Path(args[0]) if len(args) > 0 else root / "results" / "linux_bench.csv"
    qnx = Path(args[1]) if len(args) > 1 else root / "results" / "qnx_bench.csv"
    if not linux.exists():
        raise SystemExit(f"missing {linux}")
    a = load(linux)
    print("== benchmark (end-to-end RTT, bench_client) ==")
    show(a)
    if qnx.exists():
        b = load(qnx)
        show(b)
        print("\nDiferencia QNX - Linux (ms):")
        for k in ("mean", "median", "p99", "max", "stddev"):
            print(f"  {k:8} {b[k] - a[k]:+.3f}")
        plot(a, b, root / "results" / "linux_vs_qnx.png")
    else:
        print(f"\n[i] missing {qnx}. Run the benchmark inside QNX first.")
        print("    (results/linux_bench.csv is already measured; QNX results pending)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
