#!/bin/sh
# Run INSIDE the QNX VM (from the shared disk /data/share or similar).
# Measure the control_core.qnx loop with its native message-passing backend.
set -e
ENDPOINT="${ENDPOINT:-/tmp/vcr_control.sock}"
COUNT="${COUNT:-2000}"
RATE="${RATE:-20}"

echo "[qnx] endpoint: $ENDPOINT  count=$COUNT rate=${RATE}Hz"
./control_core.qnx --dry-run --endpoint "$ENDPOINT" --log core_qnx.csv &
CORE_PID=$!
sleep 1

./bench_client.qnx --endpoint "$ENDPOINT" --rate-hz "$RATE" --count "$COUNT" \
  --log bench_qnx.csv

kill "$CORE_PID" 2>/dev/null || true
wait "$CORE_PID" 2>/dev/null || true

echo
echo "[qnx] bench summary:"
tail -n 8 bench_qnx.csv 2>/dev/null || true
echo "[qnx] core: core_qnx.csv | bench: bench_qnx.csv"
