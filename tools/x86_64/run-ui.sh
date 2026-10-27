#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$PROJECT_ROOT"
QEMU="${QEMU:-qemu-system-x86_64}"
MEM="${RIOOS_MEM:-128M}"
ACCEL="${RIOOS_ACCEL:-tcg}"
DISPLAY_BACKEND="${RIOOS_DISPLAY:-gtk}"
command -v "$QEMU" >/dev/null 2>&1 || {
    echo "ERROR: qemu-system-x86_64 not found. Install the QEMU PC system emulator." >&2
    exit 1
}
if [[ ! -f build-x86_64/qemu-boot.elf || ! -f storage/disk.img ]]; then
    BUILD_SCRIPT="${RIOOS_PLATFORM_DIR:-$PROJECT_ROOT/linux}/build.sh"
    [[ -f "$BUILD_SCRIPT" ]] || { echo "ERROR: build script not found: $BUILD_SCRIPT" >&2; exit 1; }
    bash "$BUILD_SCRIPT" --skip-tests
fi
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" && "$DISPLAY_BACKEND" =~ ^(gtk|sdl)$ ]]; then
    echo "ERROR: DISPLAY/WAYLAND_DISPLAY is empty; run from a graphical desktop or set RIOOS_DISPLAY=none." >&2
    exit 1
fi
if ! "$QEMU" -display help 2>&1 | grep -qw -- "$DISPLAY_BACKEND"; then
    echo "ERROR: QEMU display backend '$DISPLAY_BACKEND' is unavailable." >&2
    echo "Try RIOOS_DISPLAY=sdl or install qemu-ui-gtk/qemu-ui-sdl." >&2
    exit 1
fi
echo "RioOS x86-64 graphical UI: QEMU standard VGA VBE + PS/2 input." >&2
echo "Use the QEMU window keyboard; arrow keys, WASD and the mouse navigate; Enter selects. COM1 remains available." >&2
exec "$QEMU" \
    -machine pc,accel="$ACCEL" \
    -cpu qemu64 -m "$MEM" \
    -kernel build-x86_64/qemu-boot.elf \
    -drive if=ide,index=0,format=raw,file=storage/disk.img \
    -netdev user,id=n1,ipv4=on,ipv6=off -device virtio-net-pci,netdev=n1,disable-modern=on \
    -vga std -display "$DISPLAY_BACKEND" \
    -serial stdio -monitor none -no-reboot -no-shutdown
