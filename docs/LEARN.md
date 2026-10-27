# Learn OS Development with RioOS x86-64

RioOS is a bare-metal codebase intended to be read as well as built. This guide gives a practical order for studying the x86-64 port without needing to read every source file at once.

## Level 1 — Understand the target

Start with the execution environment:

- x86-64 long mode and the QEMU `pc` machine
- ELF64 kernel with a small ELF32 Multiboot v1 wrapper
- COM1 serial console, 8259 PIC, PIT/TSC timing
- ATA PIO disk, PCI legacy VirtIO networking, and QEMU standard VGA / Bochs VBE

Read:

- [`../README.md`](../README.md)
- [`BUILD_AND_RUN.md`](BUILD_AND_RUN.md)
- [`X86_64_PORT.md`](X86_64_PORT.md)
- [`../linux/run.sh`](../linux/run.sh)

Questions to answer:

1. Which devices does QEMU provide to the guest?
2. Which code is shared between architectures and which code is specific to `arch/x86_64/`?
3. Why does a bare-metal kernel need a linker script and a boot wrapper?

## Level 2 — Read the boot path

Start with:

```text
arch/x86_64/qemu_multiboot32.S
arch/x86_64/qemu_multiboot32.ld
arch/x86_64/boot.S
arch/x86_64/linker.ld
```

Trace how QEMU loads the Multiboot wrapper, how it transfers control to the kernel payload, and how the bootstrap enables long mode before calling `kernel_main`.

Useful concepts:

- general-purpose registers and calling conventions
- protected mode versus long mode
- page tables and identity mapping
- GDT, IDT, privilege levels, and interrupt frames
- linker and load addresses

## Level 3 — Understand kernel initialization

Read:

```text
kernel/kernel.cpp
kernel/version.cpp
kernel/diag.cpp
arch/x86_64/platform.cpp
arch/x86_64/interrupt.cpp
```

Follow initialization in order: CPU setup, memory, storage, devices, UI/shell, and process support. Separate serial output from framebuffer output when tracing logs.

## Level 4 — Memory and processes

Read:

```text
memory/memory.cpp
memory/mmu.cpp
memory/page_alloc.cpp
process/process.cpp
process/elf.cpp
process/user.cpp
arch/x86_64/mmu.cpp
arch/x86_64/user_entry.S
```

Focus on how the user virtual-address window is mapped, how ELF load segments are validated, how ring-3 programs enter and exit, and how faults are contained to a user process.

## Level 5 — Shell, VFS, and applications

Read:

```text
kernel/shell.cpp
kernel/commands.cpp
fs/fs.cpp
fs/vfs.cpp
kernel/fd.cpp
ui/terminal.cpp
ui/apps.cpp
apps/
userspace/
```

Follow one command from input through command dispatch to filesystem or syscall activity. The `.rpk` manifests describe bundled apps; app implementations are currently built into the kernel.

## Build and test loop

```sh
bash linux/build.sh --clean
python3 tests/verify_x86_64.py
bash linux/run.sh
```

The default build runs native host tests and the x86-64 structural verifier. A real QEMU boot remains a separate runtime test. For details and current limits, see [`X86_64_PORT.md`](X86_64_PORT.md).
