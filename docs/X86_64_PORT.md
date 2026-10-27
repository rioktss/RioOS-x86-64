# MyKernel x86-64 port notes

This source tree is intentionally trimmed to the x86-64 target. Architecture-specific implementations live in `arch/x86_64/`; shared kernel, VFS, shell, file-descriptor, UI, and app code remains in the common directories. AArch64 boot code, AArch64-only drivers, user binaries, and Windows scripts for the removed target are not included.

## Boot chain

1. `linux/build-x86_64.sh` compiles a freestanding ELF64 kernel for `x86_64-unknown-none-elf`.
2. It flattens the kernel's loadable ELF segments at physical address `0x00100000`.
3. It embeds the flat payload in `arch/x86_64/qemu_multiboot32.S`, which is linked as an ELF32 Multiboot v1 wrapper.
4. QEMU's PC `-kernel` loader loads the wrapper. The wrapper copies the real kernel payload to 1 MiB and jumps to its 32-bit bootstrap.
5. `arch/x86_64/boot.S` initializes page tables, identity-maps the first 4 GiB with 2 MiB pages, enables long mode, and calls `kernel_main`.

This method requires no GRUB ISO. It assumes the bootstrap and initial devices remain within the first 4 GiB identity mapping.

## Build and test

```sh
bash linux/build-x86_64.sh --clean --verbose
python3 tests/verify_x86_64.py
bash linux/run.sh
```

The build script checks that `kernel.elf` is ELF64/EM_X86_64, that the wrapper is ELF32/i386, and that a Multiboot v1 marker exists in the wrapper's first 8 KiB. The Python verifier performs a second structural/source sanity check. These checks are **not** a substitute for an actual QEMU boot. QEMU was not available in the environment used to prepare this ZIP, so runtime status remains unverified.

## Native platform backends

| Subsystem | x86-64 implementation | Status / limits |
|---|---|---|
| Console | COM1 UART polling | Implemented |
| Input | One PS/2 decoder for keyboard + mouse into an event queue | Ctrl+letter = 1..26, arrows = 0x80..0x83, relative mouse integrated into absolute 0..32767 pointer, software cursor in `fb.cpp` |
| Storage | Primary-master ATA PIO | QEMU `if=ide`; not AHCI/NVMe |
| Clock | CMOS RTC; TSC monotonic time | TSC frequency from CPUID 0x15/0x16, else calibrated against PIT channel 2 |
| Graphics | PCI VGA BAR + Bochs/QEMU VBE LFB | QEMU standard VGA |
| Exceptions | IDT gates 0-31 plus PIC vectors 32-47 | Ring-3 faults terminate the user program only |
| IRQs | 8259 PIC remap | Masked by default; spurious IRQ7/15 handled; no APIC |
| Timer | PIT ch0 IRQ0, TSC timing | Keeps running (IF=1) while ring-3 programs run; syscalls run with IF=0 |
| SMP | single-CPU stub and spinlocks | SMP startup disabled |
| Network | virtio-net PCI legacy (I/O BAR0), polled | Needs `-device virtio-net-pci,disable-modern=on` (run scripts add it) |
| Userspace | ring 3 via `iretq`/`syscall`/`sysretq` | See below |

## Ring 3 design

- `arch/x86_64/gdt.cpp`: final GDT (0x08/0x10 kernel, 0x20 user data, 0x28 user code64), TSS, `EFER.SCE`, `STAR/LSTAR/SFMASK`.
- `arch/x86_64/user_entry.S`: `user_enter` parks callee-saved registers on the kernel stack (also `TSS.RSP0` and the syscall stack), then `iretq` to ring 3 with IF=1. `x86_syscall_entry` builds an `ExceptionFrame` (args in `x[0..5]`, number in `x[8]`) for the shared `syscall_dispatch`; `SYS_EXIT` and fatal faults return through `user_exit_return`.
- `arch/x86_64/mmu.cpp`: the 4 covering 2 MiB PDEs of the user window `0x07000000-0x07800000` are swapped for 4 KiB page tables (U/W/NX per page) while a program runs and restored afterwards. Pointer checks mirror the AArch64 `mmu_user_pointer_ok`.
- User programs: `userspace/{user_init.S,nano.cpp,calc.cpp}`, linked at `0x07000000` with `userspace/user_init.ld`.

## Testing

`bash linux/build.sh` builds, runs 15 host unit tests and `tests/verify_x86_64.py`. A real `qemu-system-x86_64` boot is still recommended.
