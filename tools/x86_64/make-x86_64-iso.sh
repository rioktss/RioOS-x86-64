#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$PROJECT_ROOT"
command -v grub-mkrescue >/dev/null 2>&1 || { echo "ERROR: grub-mkrescue not found (install GRUB rescue-image tools)." >&2; exit 1; }
command -v xorriso >/dev/null 2>&1 || { echo "ERROR: xorriso not found (required by grub-mkrescue)." >&2; exit 1; }
# ISO images default to a plain shell, matching run-terminal.sh.
# Set RIOOS_TEXT_ONLY=0 to create the regular graphical-UI ISO instead.
RIOOS_TEXT_ONLY="${RIOOS_TEXT_ONLY:-1}"
if [[ "$RIOOS_TEXT_ONLY" == "1" ]]; then
    # Rebuild so an older graphical kernel cannot accidentally be packaged.
    RIOOS_TEXT_ONLY=1 bash "$PROJECT_ROOT/tools/x86_64/build.sh" --clean --skip-tests
else
    [[ -f build-x86_64/qemu-boot.elf ]] || bash "$PROJECT_ROOT/tools/x86_64/build.sh" --skip-tests
fi
ROOT=build-x86_64/iso-root
rm -rf "$ROOT"
mkdir -p "$ROOT/boot/grub"
# GRUB multiboot1 needs a 32-bit ELF: use the wrapper that embeds the 64-bit kernel.
cp build-x86_64/qemu-boot.elf "$ROOT/boot/kernel.elf"

cat > "$ROOT/boot/grub/grub.cfg" <<'CFG'
set timeout=0
set default=0
terminal_output console
serial --unit=0 --speed=115200 --word=8 --parity=no --stop=1
terminal_input serial console
terminal_output serial console
menuentry "RioOS x86-64" {
    multiboot /boot/kernel.elf
    boot
}
CFG
grub-mkrescue -o build-x86_64/rioos-x86_64.iso "$ROOT"
echo "ISO ready: build-x86_64/rioos-x86_64.iso"
