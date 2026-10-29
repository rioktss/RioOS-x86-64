#!/usr/bin/env python3
"""Flatten kernel.elf (ELF64, loaded at 1 MiB) into kernel.bin and record the entry point."""
import struct, sys

src, dst, entry_out = sys.argv[1:4]
data = open(src, 'rb').read()
if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
    raise SystemExit('ERROR: kernel.elf is not a little-endian ELF64 file')
entry = struct.unpack_from('<Q', data, 24)[0]
phoff = struct.unpack_from('<Q', data, 32)[0]
phentsize = struct.unpack_from('<H', data, 54)[0]
phnum = struct.unpack_from('<H', data, 56)[0]
loads = []
for i in range(phnum):
    o = phoff + i * phentsize
    p_type, _flags = struct.unpack_from('<II', data, o)
    if p_type != 1:
        continue
    p_offset, p_vaddr, p_paddr, p_filesz, _memsz, _align = struct.unpack_from('<QQQQQQ', data, o + 8)
    if p_filesz:
        loads.append((p_paddr or p_vaddr, p_offset, p_filesz))
if not loads:
    raise SystemExit('ERROR: no file-backed PT_LOAD segments found')
base = min(x[0] for x in loads)
end = max(a + s for a, _o, s in loads)
if base != 0x100000:
    raise SystemExit('ERROR: expected kernel payload at 0x100000, got 0x%x' % base)
out = bytearray(end - base)
for addr, off, size in loads:
    out[addr - base:addr - base + size] = data[off:off + size]
open(dst, 'wb').write(out)
open(entry_out, 'w').write('0x%x' % entry)
