#!/usr/bin/env bash
# Boot the QNX SDP 8.0 Quick Start Target Image (QSTI) in QEMU, without hardware.
#
# The official QEMU QSTI is installed from QNX Software Center as the package
# com.qnx.qnx800.quickstart.qemu and, after running unpack_qemu_image.sh, is left
# in ~/qnx800/images/qemu/qemu/output/{ifs.bin,disk-qemu.vmdk}.
#
#   ./run-qnx.sh
#   QNX_QSTI_DIR=/path/to/qnx800/images/qemu/qemu ./run-qnx.sh
#   QNX_KERNEL=/path/ifs.bin QNX_DISK=/path/disk-qemu.vmdk ./run-qnx.sh
#
# Variables:
#   QNX_QSTI_DIR   ~/qnx800/images/qemu/qemu
#   QNX_KERNEL     $QNX_QSTI_DIR/output/ifs.bin
#   QNX_DISK       $QNX_QSTI_DIR/output/disk-qemu.vmdk
#   QNX_MEM        2048 (MB)
#   QNX_CPUS       4
#   QNX_SHARE      dist (shared FAT disk; optional)
#   QNX_SHARE_IF   virtio (default) | ide
#   QNX_GUI        0 (headless) | 1 (SDL/GL, requires QEMU 10 and virgl)
#   QNX_NET        user (default) | bridge (requires root and virbr0)
#   QNX_BRIDGE     virbr0 (if QNX_NET=bridge)
#   QNX_EXTRA_ARGS extra arguments for QEMU
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

QNX_QSTI_DIR="${QNX_QSTI_DIR:-$HOME/qnx800/images/qemu/qemu}"
QNX_KERNEL="${QNX_KERNEL:-$QNX_QSTI_DIR/output/ifs.bin}"
QNX_DISK="${QNX_DISK:-$QNX_QSTI_DIR/output/disk-qemu.vmdk}"
QNX_MEM="${QNX_MEM:-2048}"
QNX_CPUS="${QNX_CPUS:-4}"
QNX_SHARE="${QNX_SHARE:-$HERE/dist}"
QNX_SHARE_IF="${QNX_SHARE_IF:-virtio}"
QNX_NET="${QNX_NET:-user}"
QNX_GUI="${QNX_GUI:-0}"
QNX_BRIDGE="${QNX_BRIDGE:-virbr0}"

if [[ ! -f "$QNX_KERNEL" || ! -f "$QNX_DISK" ]]; then
  cat >&2 <<EOF
[!] QEMU QSTI not found.

    kernel: $QNX_KERNEL
    disk  : $QNX_DISK

Steps (summary; details in download-qnx.md):

  1. myQNX account + free non-commercial license:
     https://www.qnx.com/products/everywhere/
  2. QNX Software Center -> install the package:
       com.qnx.qnx800.quickstart.qemu
  3. Run the script it includes:
       ~/qnx800/images/qemu/unpack_qemu_image.sh
     (leaves ~/qnx800/images/qemu/qemu/output/{ifs.bin,disk-qemu.vmdk})
  4. Run this script again.

Meanwhile, validate all the logic without QNX:

    ./test-harness-linux.sh
EOF
  exit 1
fi

QEMU="qemu-system-x86_64"
command -v "$QEMU" >/dev/null || {
  echo "[!] Missing $QEMU. Install qemu-system-x86 (Debian/Ubuntu)." >&2
  exit 1
}

ACCEL=()
if [[ -w /dev/kvm ]]; then
  ACCEL=(-enable-kvm -cpu host)
else
  ACCEL=(-cpu max)
  echo "[i] /dev/kvm not available: QEMU will emulate (slower)"
fi

case "$QNX_NET" in
  bridge)
    NET=(
      -netdev "bridge,br=$QNX_BRIDGE,id=net0"
      -device "virtio-net-pci,netdev=net0,mac=52:54:00:91:01:ea"
    )
    ;;
  *)
    NET=(
      -netdev "user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::2323-:23,hostfwd=tcp::15432-:15432"
      -device "virtio-net-pci,netdev=net0"
    )
    ;;
esac

DRIVES=(-drive "file=$QNX_DISK,if=ide,id=drv0,format=vmdk")
if [[ -d "$QNX_SHARE" ]]; then
  DRIVES+=(-drive "file=fat:rw:$QNX_SHARE,format=raw,if=$QNX_SHARE_IF")
  echo "[i] sharing $QNX_SHARE as a FAT disk (vvfat)"
fi

if [[ "$QNX_GUI" == "1" ]]; then
  DISPLAY_ARGS=(-vga none -display "sdl,gl=on")
else
  DISPLAY_ARGS=(-vga none -nographic -serial mon:stdio)
fi

echo "[i] QNX QSTI: kernel=$QNX_KERNEL"
echo "[i] disk:     $QNX_DISK"
echo "[i] $QEMU ${ACCEL[*]} -m $QNX_MEM -smp $QNX_CPUS"
echo "[i] net=$QNX_NET (host 10.0.2.2 in user-mode; ssh host:2222)"
echo "[i] serial console in this terminal (Ctrl-A X to exit QEMU)"
echo

# shellcheck disable=SC2086
exec "$QEMU" "${ACCEL[@]}" -m "$QNX_MEM" -smp "$QNX_CPUS" \
  "${DRIVES[@]}" "${NET[@]}" \
  -pidfile "$HERE/qemu.pid" \
  -kernel "$QNX_KERNEL" \
  -object rng-random,filename=/dev/urandom,id=rng0 \
  -device virtio-rng-pci,rng=rng0 \
  "${DISPLAY_ARGS[@]}" \
  ${QNX_EXTRA_ARGS:-}
