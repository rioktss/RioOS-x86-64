# x86-64 validation matrix

## Automated in the build environment

- Freestanding x86-64 kernel compiles with warnings treated as errors.
- ELF64 kernel links with `ld.lld` and reports `EM_X86_64`.
- Flat loadable image is generated at physical base `0x00100000`.
- ELF32/i386 Multiboot v1 wrapper links and has the Multiboot marker in its first 8 KiB.
- Build produces a 16 MiB MyFS disk image and seeds built-in `.rpk` manifests and BMP assets.
- `tests/verify_x86_64.py` rejects missing outputs and accidental return of ARM64-only boot/driver files.

Run:

```sh
bash linux/build-x86_64.sh --clean --verbose
python3 tests/verify_x86_64.py
```

## Must still be tested with QEMU on the target host

- First boot to the serial shell and typing commands over COM1.
- ATA PIO IDENTIFY/read/write/flush and filesystem persistence after reboot.
- PS/2 keyboard navigation in the serial shell and graphical UI.
- VBE framebuffer mode setup and visible UI rendering with QEMU standard VGA.
- IRQ0/PIT periodic and one-shot timer commands.
- Reboot and recovery behavior.

The build container used to prepare this package did not provide `qemu-system-x86_64`, so these runtime checks are not claimed as completed.

## Current feature gaps

- Native x86-64 ring-3 execution, page-table-based user isolation, and syscall entry/return are not implemented.
- No PCI NIC driver, no PS/2 mouse, no APIC routing, and no SMP startup.
- Built-in GUI apps are static kernel code; `.rpk` files are manifests, not executable user packages.
