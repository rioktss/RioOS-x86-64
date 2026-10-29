#!/usr/bin/env bash
# Run RioOS and show its shell in a QEMU window (framebuffer terminal).
#
#   run.sh           -> RioOS in this host terminal (serial, -nographic)
#   run-terminal.sh  -> RioOS in a QEMU window: only the shell, black
#                       background, DejaVu Sans Mono font, scrollback with the
#                       mouse wheel (or Shift+PageUp / Shift+PageDown).
#
# The window is a plain text terminal drawn by the kernel on the QEMU VGA
# (Bochs VBE) framebuffer. Keyboard and wheel input are PS/2; the mouse is
# relative, so QEMU only delivers wheel events while it has grabbed the pointer.
# With the GTK backend the window uses grab-on-hover (move the pointer over the
# window; release with Ctrl+Alt+G). PageUp/PageDown always scroll. The serial
# console also stays attached to this host terminal, so boot messages remain
# visible here.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

QEMU="${QEMU:-qemu-system-x86_64}"
KERNEL="${KERNEL:-build/qemu-boot.elf}"
DISK="${DISK:-storage/disk.img}"
MEM="${RIOOS_MEM:-128M}"
ACCEL="${RIOOS_ACCEL:-tcg}"
DISPLAY_BACKEND="${RIOOS_DISPLAY:-gtk}"

command -v "$QEMU" >/dev/null 2>&1 || {
    echo "ERROR: qemu-system-x86_64 not found." >&2
    echo "  Arch Linux : sudo pacman -S qemu-system-x86 qemu-ui-gtk qemu-ui-sdl" >&2
    echo "  Debian/Ubuntu: sudo apt install qemu-system-x86" >&2
    echo "  Termux: use a QEMU build with GTK or SDL display support." >&2
    exit 1
}
[[ -f "$KERNEL" ]] || { echo "ERROR: $KERNEL not found. Run the build script for this platform first." >&2; exit 1; }
[[ -f "$DISK" ]] || { echo "ERROR: $DISK not found. Run the build script for this platform first." >&2; exit 1; }

# A window needs a desktop session (X11 / Wayland / WSLg / Termux:X11).
HOST_KIND="$(uname -s 2>/dev/null || true)"
IS_NATIVE_WINDOWS=0
case "$HOST_KIND" in MINGW*|MSYS*|CYGWIN*) IS_NATIVE_WINDOWS=1 ;; esac
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" && "$IS_NATIVE_WINDOWS" -eq 0 ]]; then
    echo "ERROR: no graphical session detected (DISPLAY / WAYLAND_DISPLAY is empty)." >&2
    echo "  Run this from a terminal inside your desktop, or use run.sh for a plain terminal." >&2
    exit 1
fi

# Arch's base qemu packages ship without GUI backends; detect that early
# instead of letting QEMU open no window at all.
backend_ok() { "$QEMU" -display help 2>&1 | grep -qw -- "$1"; }
if ! backend_ok "$DISPLAY_BACKEND"; then
    picked=""
    for candidate in gtk sdl; do
        if backend_ok "$candidate"; then picked="$candidate"; break; fi
    done
    if [[ -z "$picked" ]]; then
        echo "ERROR: this QEMU build has no usable GTK/SDL display backend." >&2
        echo "  Arch Linux : sudo pacman -S qemu-ui-gtk qemu-ui-sdl (or qemu-full)" >&2
        exit 1
    fi
    echo "NOTE: display backend '$DISPLAY_BACKEND' unavailable; using '$picked'." >&2
    DISPLAY_BACKEND="$picked"
fi

DISPLAY_ARGS="$DISPLAY_BACKEND"
[[ "$DISPLAY_BACKEND" == "gtk" ]] && DISPLAY_ARGS="gtk,grab-on-hover=on"

echo "RioOS terminal window: QEMU ($DISPLAY_BACKEND). Type in the window; scroll with the mouse wheel (pointer over the window) or PageUp/PageDown." >&2

exec "$QEMU" \
    -machine pc,accel="$ACCEL" \
    -cpu qemu64 \
    -m "$MEM" \
    -vga std \
    -display "$DISPLAY_ARGS" \
    -serial stdio \
    -monitor none \
    -no-reboot \
    -kernel "$KERNEL" \
    -drive "if=ide,index=0,format=raw,file=$DISK" \
    -netdev user,id=n1,ipv4=on,ipv6=off \
    -device virtio-net-pci,netdev=n1,disable-modern=on
