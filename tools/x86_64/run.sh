#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$PROJECT_ROOT"
QEMU="${QEMU:-qemu-system-x86_64}"
command -v "$QEMU" >/dev/null 2>&1 || {
    echo "ERROR: qemu-system-x86_64 not found." >&2
    echo "Arch Linux: sudo pacman -S qemu-system-x86" >&2
    echo "Termux: pkg install qemu-system-x86-64 (if available in your repository)." >&2
    exit 1
}
if [[ ! -f build-x86_64/qemu-boot.elf || ! -f storage/disk.img ]]; then
    BUILD_SCRIPT="${RIOOS_PLATFORM_DIR:-$PROJECT_ROOT/linux}/build.sh"
    [[ -f "$BUILD_SCRIPT" ]] || { echo "ERROR: build script not found: $BUILD_SCRIPT" >&2; exit 1; }
    echo "[RioOS] Build output missing; building before launch..." >&2
    bash "$BUILD_SCRIPT" --skip-tests
fi
[[ -f build-x86_64/qemu-boot.elf ]] || { echo "ERROR: build did not produce build-x86_64/qemu-boot.elf" >&2; exit 1; }
[[ -f storage/disk.img ]] || { echo "ERROR: build did not produce storage/disk.img" >&2; exit 1; }
MEM="${RIOOS_MEM:-128M}"
ACCEL="${RIOOS_ACCEL:-tcg}"
exec "$QEMU" \
    -machine pc,accel="$ACCEL" \
    -cpu qemu64 \
    -m "$MEM" \
    -kernel build-x86_64/qemu-boot.elf \
    -drive if=ide,index=0,format=raw,file=storage/disk.img \
    -netdev user,id=n1,ipv4=on,ipv6=off -device virtio-net-pci,netdev=n1,disable-modern=on \
    -vga none -display none -serial stdio -monitor none \
    -no-reboot -no-shutdown
