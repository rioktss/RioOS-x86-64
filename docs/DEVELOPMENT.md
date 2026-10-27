# x86-64 MyKernel development guide

## Development principles

Make small changes that keep hardware assumptions explicit. Kernel code runs freestanding with a small custom runtime; it does not have the normal C++ standard library or host OS services. Changes that affect boot, memory, IRQs, the IDE disk, VBE framebuffer, or syscall ABI should be isolated and documented.

## Build and verification

```sh
bash linux/build-x86_64.sh --clean --verbose
python3 tests/verify_x86_64.py
```

The build compiles the architecture-specific assembly and C++ backend, links the ELF64 kernel, creates a flat kernel payload and links the ELF32 Multiboot wrapper. The structural verifier checks the machine IDs, Multiboot header, generated disk, and expected source set. The verifier is not a runtime emulator and cannot prove the kernel will boot on every QEMU build.

## Runtime test loop

```sh
bash linux/run.sh       # serial shell
bash linux/run-ui.sh    # graphical VGA/VBE UI, from a desktop session
```

Capture the entire serial log when investigating a failure. Decide whether it occurs during boot, memory setup, device init, input, filesystem access, graphics, or shell dispatch. Rebuild cleanly and rerun the structural verifier after changes. On a host with QEMU, also verify the real boot and relevant device behavior; the current source build has not been runtime-verified in the build environment.

## Current architecture assumptions

- Target triple: `x86_64-unknown-none-elf`; Multiboot v1 ELF32 shim starts the real ELF64 kernel at 1 MiB.
- Bootstrap identity maps the first 4 GiB with 2 MiB pages.
- Serial console is COM1; keyboard is polled PS/2.
- Persistent storage uses primary-master ATA PIO on the QEMU PC machine.
- Graphics uses QEMU standard VGA / Bochs VBE, not the AArch64 `ramfb` path.
- PIC IRQs are masked during ordinary boot; timer mode defaults to OFF.
- SMP startup/APIC are not implemented (single CPU). Ring-3 execution, syscall entry and virtio-net PCI networking are implemented.

## Host-side tests

The `tests/` directory retains shared subsystem regression tests. The default x86 build intentionally does not claim that all legacy AArch64-oriented host tests passed. `tests/TEST_MATRIX.md` separates checks actually automated from runtime checks that still need QEMU.
