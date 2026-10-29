# P5 — Per-process ring 3 address-space isolation

RioOS v1.12.0 introduces independent user memory for each PID while preserving the existing shell, RPK UI, filesystem, networking, syscall, and cooperative kernel-thread features.

## What changed

- `memory/mmu.cpp` maintains a PID-keyed address-space table. Each address space has a private PML4/PDPT/PD chain and four private 4 KiB page tables covering the ELF image and stack range (user window 0x07000000-0x07800000).
- Device mappings and ordinary kernel RAM mappings are shared with the baseline kernel tables. The user ELF window (`0x47000000`–`0x47800000`) is unmapped in each new address space until a segment is mapped.
- ELF `PT_LOAD` segments receive allocated physical frames. Segment bytes are copied through the allocator's kernel-visible physical alias, not by writing to a common identity-mapped user address.
- Each process owns a 64 KiB RW/NX stack at the top of the reserved stack window. The preceding stack region is unmapped as a guard.
- The loader finalizes page tables and synchronizes executable-page caches. `user_execute()` activates the PID's CR3 before entering ring 3 and restores the baseline kernel CR3 immediately after returning to ring 0.
- Address-space destruction releases all owned image and stack frames, including common load-failure cleanup paths.
- The PCB can retain a complete ring 3 general-register snapshot plus RIP, RFLAGS and the ring 3 stack pointer. The exception handler records this snapshot at a synchronous ring 3 exception boundary.

## Runtime contract and limitations

P5 makes address spaces independent; it does not yet make ring 3 processes run concurrently. `user_execute()` still enters a process synchronously from the shell, and, since 1.13.0, the 100 Hz timer IRQ stays live while ring 3 runs (IF set); it only counts ticks and never switches processes. `ProcessCpuContext` is saved/restorable metadata, but the scheduler does not yet switch from one suspended ring 3 exception frame to another. That requires a separate kernel stack and a carefully validated exception-return path for each schedulable user process.

The reserved address layout is specific to the current QEMU `pc` identity-map design and 128 MiB memory map. The process page-table metadata is statically reserved; physical user pages come from the existing page allocator. Exhaustion must fail the load rather than silently falling back to a shared image.

## Build and verification

Run `bash linux/build.sh --clean` or the platform-equivalent build script. It compiles the x86-64 image, runs host regressions, and runs `tests/verify_x86_64.py`. Host tests validate PCB context retention and general regressions. A successful host build does not replace a QEMU runtime test of `/bin/init.elf`, `/bin/nano.elf`, and `/bin/calc.elf`.
