#!/usr/bin/env python3
"""Validate the x86-64 kernel ELF and the QEMU Multiboot loader wrapper."""
from __future__ import annotations

import os
import struct
import sys

ELF_MAGIC = b"\x7fELF"
PT_LOAD = 1
EM_386 = 3
EM_X86_64 = 62
ET_EXEC = 2
MULTIBOOT_MAGIC = 0x1BADB002


def fail(message: str) -> None:
    raise SystemExit(f"ERROR: {message}")


def read_file(path: str) -> bytes:
    try:
        with open(path, "rb") as handle:
            return handle.read()
    except OSError as exc:
        fail(f"cannot read {path}: {exc}")


def elf_header(data: bytes, expected_class: int, expected_machine: int, label: str):
    if len(data) < 64 or data[:4] != ELF_MAGIC:
        fail(f"{label} is not an ELF file")
    if data[4] != expected_class or data[5] != 1 or data[6] != 1:
        fail(f"{label} must be ELF{32 if expected_class == 1 else 64}, little-endian, version 1")
    if expected_class == 2:
        e_type, machine = struct.unpack_from("<HH", data, 16)
        entry, phoff = struct.unpack_from("<QQ", data, 24)
        phentsize, phnum = struct.unpack_from("<HH", data, 54)
        fmt, min_size = "<IIQQQQQQ", 56
    else:
        e_type, machine = struct.unpack_from("<HH", data, 16)
        entry, phoff = struct.unpack_from("<II", data, 24)
        phentsize, phnum = struct.unpack_from("<HH", data, 42)
        fmt, min_size = "<IIIIIIII", 32
    if e_type != ET_EXEC:
        fail(f"{label} must be ET_EXEC")
    if machine != expected_machine:
        fail(f"{label} has unexpected e_machine={machine}")
    if phentsize < min_size or phoff + phentsize * phnum > len(data):
        fail(f"{label} has an invalid program-header table")

    loads = []
    for index in range(phnum):
        off = phoff + index * phentsize
        fields = struct.unpack_from(fmt, data, off)
        if expected_class == 2:
            p_type, _flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, _align = fields
            load_address = p_paddr or p_vaddr
        else:
            p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, _flags, _align = fields
            load_address = p_paddr or p_vaddr
        if p_type == PT_LOAD:
            if p_offset + p_filesz > len(data) or p_memsz < p_filesz:
                fail(f"{label} contains an invalid PT_LOAD segment")
            loads.append((load_address, load_address + p_memsz))
    if not loads:
        fail(f"{label} has no PT_LOAD segment")
    if not any(start <= entry < end for start, end in loads):
        fail(f"{label} entry point is outside its PT_LOAD segments")
    return entry, loads


def main() -> None:
    kernel_path = "build/kernel.elf"
    flat_path = "build/kernel.bin"
    entry_path = "build/kernel.entry"
    wrapper_path = "build/qemu-boot.elf"

    kernel = read_file(kernel_path)
    entry, segments = elf_header(kernel, 2, EM_X86_64, "kernel.elf")
    if entry == 0:
        fail("kernel entry point is zero")
    if not any(start == 0x100000 for start, _end in segments):
        fail("kernel PT_LOAD image is not based at physical 0x100000")

    flat = read_file(flat_path)
    if len(flat) < 4096 or len(flat) > 32 * 1024 * 1024:
        fail("kernel.bin has an implausible size")
    try:
        recorded_entry = int(read_file(entry_path).decode("ascii").strip(), 0)
    except (ValueError, UnicodeDecodeError):
        fail("kernel.entry is not a valid address")
    if recorded_entry != entry:
        fail("kernel.entry does not match kernel.elf e_entry")

    wrapper = read_file(wrapper_path)
    _wrapper_entry, _wrapper_segments = elf_header(wrapper, 1, EM_386, "qemu-boot.elf")
    if flat not in wrapper:
        fail("qemu-boot.elf does not contain the generated kernel.bin payload")

    # Multiboot v1 header must be in the first 8 KiB of the ELF file.
    header_ok = False
    scan_end = min(len(wrapper), 8192)
    for offset in range(0, max(0, scan_end - 11), 4):
        if struct.unpack_from("<I", wrapper, offset)[0] != MULTIBOOT_MAGIC:
            continue
        magic, flags, checksum = struct.unpack_from("<III", wrapper, offset)
        if (magic + flags + checksum) & 0xFFFFFFFF == 0:
            header_ok = True
            break
    if not header_ok:
        fail("valid Multiboot v1 header not found in first 8 KiB")

    print("x86-64 ELF: OK")
    print("QEMU Multiboot wrapper: OK")
    print(f"Kernel entry: 0x{entry:x}; flat payload: {len(flat)} bytes")


if __name__ == "__main__":
    main()
