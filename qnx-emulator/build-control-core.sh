#!/usr/bin/env bash
# Cross-compile control_core for QNX SDP 8.0 with qcc and leave the binaries
# in dist/ to copy them to the QEMU VM (shared FAT disk).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
CORE="$REPO/control_core"

QNX_ENV="${QNX_ENV:-}"
if [[ -z "$QNX_ENV" ]]; then
  for candidate in \
    "$HOME/qnx800/qnxsdp-env.sh" \
    "/opt/qnx800/qnxsdp-env.sh" \
    "$HOME/qnx/qnxsdp-env.sh" \
    "/opt/qnx/qnxsdp-env.sh"; do
    [[ -f "$candidate" ]] && QNX_ENV="$candidate" && break
  done
fi
if [[ -z "${QNX_ENV:-}" || ! -f "$QNX_ENV" ]]; then
  cat >&2 <<EOF
[!] qnxsdp-env.sh not found. Set it explicitly:

    QNX_ENV=/ruta/a/qnx800/qnxsdp-env.sh ./build-control-core.sh

QNX SDP 8.0 is free for non-commercial use:
    https://www.qnx.com/products/everywhere/
EOF
  exit 1
fi

ARCH="${QNX_ARCH:-x86_64}"
echo "[i] sourcing $QNX_ENV"
# shellcheck disable=SC1090
source "$QNX_ENV"

echo "[i] building for QNX_ARCH=$ARCH"
make -C "$CORE" clean
make -C "$CORE" qnx QNX_ARCH="$ARCH"

mkdir -p "$HERE/dist"
cp "$CORE/build/qnx/control_core.qnx" "$HERE/dist/"
cp "$CORE/build/qnx/bench_client.qnx" "$HERE/dist/"
cp "$CORE/python_client/control_client.py" "$HERE/dist/" 2>/dev/null || true
cp "$HERE/bench-inside-qnx.sh" "$HERE/dist/"

echo
echo "[ok] binaries in $HERE/dist:"
ls -la "$HERE/dist"
echo
echo "Next step: ./run-qnx.sh"
