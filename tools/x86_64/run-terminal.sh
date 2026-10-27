#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$PROJECT_ROOT"

QEMU="${QEMU:-qemu-system-x86_64}"
MEM="${RIOOS_MEM:-128M}"
ACCEL="${RIOOS_ACCEL:-tcg}"
DISPLAY_BACKEND="${RIOOS_DISPLAY:-gtk}"
ENTRY_DIR="${RIOOS_PLATFORM_DIR:-$PROJECT_ROOT/linux}"

command -v "$QEMU" >/dev/null 2>&1 || {
    echo "ERROR: qemu-system-x86_64 not found." >&2
    echo "  Debian/Ubuntu: sudo apt install qemu-system-x86" >&2
    echo "  Arch Linux: sudo pacman -S qemu-system-x86 qemu-ui-gtk qemu-ui-sdl" >&2
    echo "  Termux: install a QEMU build with graphical display support, if available." >&2
    exit 1
}
if [[ ! -f build-x86_64/qemu-boot.elf || ! -f storage/disk.img ]]; then
    [[ -f "$ENTRY_DIR/build.sh" ]] || { echo "ERROR: build script not found: $ENTRY_DIR/build.sh" >&2; exit 1; }
    bash "$ENTRY_DIR/build.sh" --skip-tests
fi

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" && "$DISPLAY_BACKEND" =~ ^(gtk|sdl)$ ]]; then
    echo "ERROR: no graphical desktop session detected (DISPLAY/WAYLAND_DISPLAY is empty)." >&2
    echo "  Linux: run from a desktop terminal; Android: start Termux:X11 first." >&2
    echo "  macOS: use RIOOS_DISPLAY=cocoa if supported by your QEMU build." >&2
    exit 1
fi
if ! "$QEMU" -display help 2>&1 | grep -qw -- "$DISPLAY_BACKEND"; then
    echo "ERROR: QEMU display backend '$DISPLAY_BACKEND' is unavailable." >&2
    echo "Try RIOOS_DISPLAY=sdl or RIOOS_DISPLAY=gtk (whichever your QEMU build supports)." >&2
    exit 1
fi

echo "RioOS x86-64 terminal: opening a plain black QEMU text-console window ($DISPLAY_BACKEND)." >&2
echo "This shows the original serial shell, not the pixel UI Terminal app." >&2
exec "$QEMU" \
    -machine pc,accel="$ACCEL" \
    -cpu qemu64 -m "$MEM" \
    -kernel build-x86_64/qemu-boot.elf \
    -drive if=ide,index=0,format=raw,file=storage/disk.img \
    -netdev user,id=n1,ipv4=on,ipv6=off -device virtio-net-pci,netdev=n1,disable-modern=on \
    -vga none -display "$DISPLAY_BACKEND" \
    -serial vc:100Cx32C -monitor none -no-reboot -no-shutdown
