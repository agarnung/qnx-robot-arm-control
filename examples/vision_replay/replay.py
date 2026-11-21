#!/usr/bin/env python3
"""Replay a recorded joint-target sequence through control_core.

This is not MediaPipe and it does not open a camera. It replays the *output* of
the upstream vision pipeline (joint targets over time) through the same 108-byte
IPC protocol that the real vision process uses. It is a deterministic way to test
the vision -> control_core -> robot path without a camera or MediaPipe.

The JSON format is deliberately small:

    {"keyframes": [{"t": 0.0, "joints": [q0, ..., q5]}, ...]}

Keyframes are linearly interpolated at --rate-hz. Replace session.json with a
recording from the upstream vision component to replay a real session.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "control_core" / "python_client"))

from control_client import (  # noqa: E402
    ControlCoreClient,
    VCR_MODE_MOVEJ,
    VCR_MODE_SERVOJ,
)


def load_keyframes(path: Path) -> list[tuple[float, list[float]]]:
    data = json.loads(path.read_text())
    raw = data.get("keyframes")
    if not isinstance(raw, list) or len(raw) < 2:
        raise SystemExit(f"{path}: expected at least two keyframes")
    frames: list[tuple[float, list[float]]] = []
    for i, item in enumerate(raw):
        t = float(item["t"])
        q = [float(x) for x in item["joints"]]
        if len(q) != 6:
            raise SystemExit(f"{path}: keyframe {i} needs exactly 6 joints")
        frames.append((t, q))
    frames.sort(key=lambda x: x[0])
    if frames[-1][0] <= frames[0][0]:
        raise SystemExit(f"{path}: keyframe times must increase")
    return frames


def lerp(a: list[float], b: list[float], u: float) -> list[float]:
    return [x + (y - x) * u for x, y in zip(a, b)]


def sample(frames: list[tuple[float, list[float]]], t: float) -> list[float]:
    if t <= frames[0][0]:
        return frames[0][1]
    if t >= frames[-1][0]:
        return frames[-1][1]
    for (t0, q0), (t1, q1) in zip(frames, frames[1:]):
        if t0 <= t <= t1:
            u = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
            return lerp(q0, q1, u)
    return frames[-1][1]


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--endpoint", default=os.environ.get("CONTROL_CORE_ENDPOINT", "/tmp/vcr_control.sock"))
    p.add_argument("--json", type=Path, default=Path(__file__).with_name("session.json"))
    p.add_argument("--rate-hz", type=float, default=20.0)
    p.add_argument("--mode", choices=("movej", "servoj"), default="servoj")
    p.add_argument("--dry-run", action="store_true", help="send dry_run=1 to control_core")
    p.add_argument("--loop", action="store_true", help="repeat the sequence until interrupted")
    p.add_argument("--probe", action="store_true", help="probe_home before replaying")
    args = p.parse_args()

    if args.rate_hz <= 0:
        raise SystemExit("--rate-hz must be > 0")
    frames = load_keyframes(args.json)
    total = frames[-1][0] - frames[0][0]
    mode = VCR_MODE_SERVOJ if args.mode == "servoj" else VCR_MODE_MOVEJ
    period = 1.0 / args.rate_hz
    n = max(1, int(total * args.rate_hz))

    with ControlCoreClient(args.endpoint, timeout=2.0) as core:
        if args.probe:
            st = core.probe_home()
            print(f"probe: ok={st.ok} telemetry_valid={st.telemetry_valid} "
                  f"q_actual={['%.3f' % x for x in st.q_actual]}")
        print(f"replay: {args.json}  {total:.2f}s  {args.rate_hz:.1f} Hz  mode={args.mode}")
        start = time.monotonic()
        sent = 0
        try:
            while True:
                for i in range(n):
                    t = i * period
                    q = sample(frames, frames[0][0] + t)
                    st = core.send_motion(
                        q, 0.5, 0.5, mode=mode, dry_run=args.dry_run,
                        servoj_t=period, servoj_lookahead=0.1, servoj_gain=300,
                    )
                    if not st.ok:
                        print(f"control_core error at t={t:.3f}s: code={st.error_code}", file=sys.stderr)
                        return 2
                    sent += 1
                    if sent % max(1, int(args.rate_hz)) == 0:
                        print(f"  {t:5.2f}s  seq={st.seq}  ok={st.ok}  "
                              f"robot_connected={st.robot_connected}")
                    target = start + (sent * period)
                    delay = target - time.monotonic()
                    if delay > 0:
                        time.sleep(delay)
                if not args.loop:
                    break
                start = time.monotonic()
        except KeyboardInterrupt:
            print("\ninterrupted")
        print(f"replayed {sent} targets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
