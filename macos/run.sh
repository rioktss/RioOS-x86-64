#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

QEMU="${QEMU:-qemu-system-x86_64}"
KERNEL="${KERNEL:-build/qemu-boot.elf}"
DISK="${DISK:-storage/disk.img}"
MEM="${RIOOS_MEM:-128M}"
ACCEL="${RIOOS_ACCEL:-tcg}"

command -v "$QEMU" >/dev/null 2>&1 || {
    echo "ERROR: qemu-system-x86_64 not found."
    exit 1
}

[[ -f "$KERNEL" ]] || { echo "ERROR: $KERNEL not found. Run the build script for this platform first."; exit 1; }
[[ -f "$DISK" ]] || { echo "ERROR: $DISK not found. Run the build script for this platform first."; exit 1; }

exec "$QEMU" \
    -machine pc,accel="$ACCEL" \
    -cpu qemu64 \
    -m "$MEM" \
    -nographic \
    -vga none \
    -monitor none \
    -no-reboot \
    -kernel "$KERNEL" \
    -drive if=ide,index=0,format=raw,file="$DISK" \
    -netdev user,id=n1,ipv4=on,ipv6=off \
    -device virtio-net-pci,netdev=n1,disable-modern=on
