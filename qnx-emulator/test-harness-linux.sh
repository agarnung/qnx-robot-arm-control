#!/usr/bin/env bash
# Validate the full harness on Linux (no QNX): mock UR + control_core + bench.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
CORE="$REPO/control_core"
PY="${PYTHON:-python3}"
SOCK="/tmp/vcr_qnx_emulator_test.sock"
MOCK_LOG="/tmp/mock_ur_$(date +%s).log"

cleanup() {
  [[ -n "${MOCK_PID:-}" ]] && kill "$MOCK_PID" 2>/dev/null || true
  [[ -n "${CORE_PID:-}" ]] && kill "$CORE_PID" 2>/dev/null || true
  rm -f "$SOCK"
}
trap cleanup EXIT

echo "[1/4] building control_core (POSIX)..."
make -C "$CORE" >/dev/null

echo "[2/4] starting simulated UR server..."
"$PY" "$HERE/mock_ur_server.py" --host 127.0.0.1 --port 30003 --hz 20 --log "$MOCK_LOG" &
MOCK_PID=$!
sleep 1

echo "[3/4] starting control_core against the mock (with telemetry)..."
"$CORE/build/control_core" --robot-ip 127.0.0.1 --endpoint "$SOCK" \
  --with-telemetry --log /tmp/core_qnx_emulator_test.csv &
CORE_PID=$!
sleep 1

echo "[4/4] running bench_client..."
"$CORE/build/bench_client" --endpoint "$SOCK" --rate-hz 20 --count 100 \
  --log /tmp/bench_qnx_emulator_test.csv

echo
echo "OK. UR commands recorded:"
grep -c "CMD" "$MOCK_LOG" || true
echo "Mock log: $MOCK_LOG"
echo "control_core CSV: /tmp/core_qnx_emulator_test.csv"
echo "bench CSV:        /tmp/bench_qnx_emulator_test.csv"
