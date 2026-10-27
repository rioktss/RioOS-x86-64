#!/usr/bin/env python3
"""Flatten kernel.elf to kernel.bin and build the ELF32 Multiboot wrapper (used by build.ps1)."""
import argparse, struct, subprocess, sys
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument("kernel_elf"); ap.add_argument("kernel_bin"); ap.add_argument("wrapper")
ap.add_argument("--clang", default="clang"); ap.add_argument("--ld", default="ld.lld")
a = ap.parse_args()
root = Path(__file__).resolve().parents[1]
data = Path(a.kernel_elf).read_bytes()
if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
    sys.exit("ERROR: kernel.elf is not a little-endian ELF64 file")
phoff = struct.unpack_from("<Q", data, 32)[0]
phentsize, phnum = struct.unpack_from("<HH", data, 54)
loads = []
for i in range(phnum):
    o = phoff + i * phentsize
    if struct.unpack_from("<I", data, o)[0] != 1:
        continue
    p_offset, p_vaddr, p_paddr, p_filesz = struct.unpack_from("<QQQQ", data, o + 8)
    if p_filesz:
        loads.append((p_paddr or p_vaddr, p_offset, p_filesz))
base = min(x[0] for x in loads)
if base != 0x100000:
    sys.exit(f"ERROR: expected kernel payload at 0x100000, got 0x{base:x}")
end = max(x[0] + x[2] for x in loads)
out = bytearray(end - base)
for addr, off, size in loads:
    out[addr - base:addr - base + size] = data[off:off + size]
Path(a.kernel_bin).write_bytes(out)
entry = struct.unpack_from("<Q", data, 24)[0]
obj = str(Path(a.wrapper).with_suffix(".o"))
subprocess.check_call([a.clang, "--target=i386-unknown-none-elf", "-m32", "-ffreestanding", "-fno-pic", "-fno-pie",
                       f"-DKERNEL_ENTRY_ADDRESS=0x{entry:x}", "-c", str(root / "arch/x86_64/qemu_multiboot32.S"), "-o", obj])
subprocess.check_call([a.ld, "-m", "elf_i386", "-T", str(root / "arch/x86_64/qemu_multiboot32.ld"), "-nostdlib", obj, "-o", a.wrapper])
print(f"boot image ready: {a.wrapper}")
