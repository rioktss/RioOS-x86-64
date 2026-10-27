
#!/usr/bin/env python3
"""Structural verification for the x86-64 MyKernel port (not a runtime test)."""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build-x86_64"
kernel = OUT / "kernel.elf"
wrapper = OUT / "qemu-boot.elf"
flat = OUT / "kernel.bin"
disk = ROOT / "storage" / "disk.img"

for path in (kernel, wrapper, flat, disk):
    assert path.is_file() and path.stat().st_size > 0, f"missing/empty build artifact: {path.relative_to(ROOT)}"

kb = kernel.read_bytes()
assert kb[:4] == b"\x7fELF" and kb[4] == 2 and kb[5] == 1, "kernel must be little-endian ELF64"
assert struct.unpack_from("<H", kb, 18)[0] == 62, "kernel ELF machine must be EM_X86_64"
entry = struct.unpack_from("<Q", kb, 24)[0]
assert 0x100000 <= entry < 0x100000000, f"unexpected kernel entry: 0x{entry:x}"

wb = wrapper.read_bytes()
assert wb[:4] == b"\x7fELF" and wb[4] == 1 and wb[5] == 1, "QEMU wrapper must be little-endian ELF32"
assert struct.unpack_from("<H", wb, 18)[0] == 3, "wrapper ELF machine must be i386"
assert b"\x02\xb0\xad\x1b" in wb[:8192], "Multiboot v1 header must be within first 8 KiB"
assert disk.stat().st_size == 16 * 1024 * 1024, "generated test disk should be 16 MiB"

# Guard dimatiin total
pass

sources = {
    "arch/x86_64/boot.S": ("long_mode_start", "boot_pd3"),
    "arch/x86_64/platform.cpp": ("ATA_COMMAND", "PS/2", "ps2_pump", "mouse_byte"),
    "arch/x86_64/fb.cpp": ("VBE_INDEX", "FB_DEFAULT_WIDTH", "fb_present"),
    "arch/x86_64/interrupt.cpp": ("0x20", "x86_irq_dispatch"),
    "arch/x86_64/timer.cpp": ("pit_set_hz", "rdtsc", "timer_interrupt_handler", "calibrate_tsc_with_pit"),
    "arch/x86_64/gdt.cpp": ("x86_user_init", "ltr", "0xC0000082"),
    "arch/x86_64/user_entry.S": ("user_enter", "user_exit_return", "sysretq", "iretq"),
    "arch/x86_64/mmu.cpp": ("mmu_user_finish", "user_pt", "NXE"),
    "arch/x86_64/net.cpp": ("0x1AF4", "net_device_send", "net_device_receive"),
}
for name, markers in sources.items():
    text = (ROOT / name).read_text()
    for marker in markers:
        assert marker in text, f"expected x86 implementation marker {marker!r} in {name}"

for prog in ("user_init", "nano", "calc"):
    p = OUT / "user" / f"{prog}.elf"
    assert p.is_file(), f"missing userspace ELF: {p.relative_to(ROOT)}"
    b = p.read_bytes()
    assert b[:4] == b"\x7fELF" and struct.unpack_from("<H", b, 18)[0] == 62, f"{prog}.elf must be an x86-64 ELF"
    entry = struct.unpack_from("<Q", b, 24)[0]
    assert 0x07000000 <= entry < 0x07700000, f"{prog}.elf entry outside the user window: 0x{entry:x}"
data = disk.read_bytes()
for name in (b"nano.elf", b"calc.elf", b"init.elf"):
    assert name in data, f"{name.decode()} is not installed in the disk image"

print("x86-64 source + ELF/Multiboot structural checks passed (QEMU runtime is a separate test).")
