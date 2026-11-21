#!/usr/bin/env bash
# Record the Linux/POSIX terminal session for the video clips.
# Usage: ./evidence/record-linux-baseline.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
CORE="$ROOT/control_core"
LOG="$HERE/linux-baseline.log"

exec >"$LOG" 2>&1

mark() { echo; echo "@@CLIP:$1@@"; }
run() { echo; echo "\$ $*"; "$@"; }

echo "=== Linux/POSIX baseline — host: $(uname -sr) ==="

mark 01-linux-build
echo; echo "\$ qcc --version"
bash -lc 'source "$HOME/qnx800/qnxsdp-env.sh" >/dev/null 2>&1; qcc --version; qcc -Vgcc_ntox86_64 --version'
echo; echo "\$ file qnx-emulator/dist/control_core.qnx"
file "$ROOT/qnx-emulator/dist/control_core.qnx"
cd "$CORE"
run make clean
run make
echo; echo "\$ file build/control_core build/bench_client"
file build/control_core build/bench_client

mark 02-linux-tests
run make test

mark 03-linux-hardening
run make sanitize
run make analyze

mark 04-linux-bench
run make clean
run make
echo; echo "\$ ./build/control_core --dry-run --endpoint /tmp/vcr_rec.sock --log $HERE/linux_core_replay.csv &"
./build/control_core --dry-run --endpoint /tmp/vcr_rec.sock --log "$HERE/linux_core_replay.csv" &
CORE=$!
sleep 1
echo; echo "\$ ./build/bench_client --endpoint /tmp/vcr_rec.sock --rate-hz 20 --count 500 --log $HERE/linux_bench_replay.csv"
./build/bench_client --endpoint /tmp/vcr_rec.sock --rate-hz 20 --count 500 --log "$HERE/linux_bench_replay.csv"
kill "$CORE" 2>/dev/null || true
wait "$CORE" 2>/dev/null || true

mark 05-linux-compare
cd "$ROOT"
echo; echo "\$ python3 compare_results.py"
python3 compare_results.py

echo
echo "=== end: $(date -u '+%Y-%m-%dT%H:%M:%SZ') ==="
