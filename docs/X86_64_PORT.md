# RioOS x86-64 notes (v1.13.0)

The x86-64 tree has the same folder layout and feature set as the AArch64 tree
(`linux/ macos/ termux/ windows/`, `boot/ drivers/ kernel/ memory/ process/ fs/
net/ storage/ tests/ userspace/ docs/ tools/`). Only the architecture-specific
files differ.

## Boot chain

1. `linux/build.sh` compiles a freestanding ELF64 kernel (`x86_64-unknown-none-elf`).
2. `tools/make_boot_image.py` flattens its PT_LOAD segments at `0x00100000` (`build/kernel.bin`).
3. `boot/qemu_multiboot32.S` embeds that payload in an ELF32 Multiboot v1 wrapper (`build/qemu-boot.elf`).
4. QEMU's `-kernel` loader starts the wrapper, which copies the payload to 1 MiB and jumps to `boot/boot.S`.
5. `boot/boot.S` builds page tables (first 4 GiB identity-mapped with 2 MiB pages), enters long mode and calls `kernel_main`.

## Where the x86-64 specific code lives

| File | Role |
|---|---|
| `boot/boot.S`, `boot/linker.ld`, `boot/exceptions.S` | long-mode bootstrap, link layout, exception/IRQ stubs |
| `boot/qemu_multiboot32.{S,ld}` | Multiboot v1 ELF32 wrapper |
| `kernel/gdt.cpp` | GDT, TSS, `EFER.SCE`, `STAR/LSTAR/SFMASK` (SYSCALL) |
| `kernel/exceptions.cpp` | IDT, CPU exception handling (ring 3 faults end only the program) |
| `kernel/interrupt.cpp`, `kernel/timer.cpp` | 8259 PIC, 8254 PIT 100 Hz tick, TSC clock, `interrupt_idle()` (`hlt`) |
| `kernel/rtc.cpp` | CMOS RTC |
| `memory/mmu.cpp` | paging, per-process page tables (PML4/PDPT/PD + four 4 KiB tables) |
| `process/user_entry.S`, `process/ctx_switch.S` | ring 3 entry/exit (`iretq`, `syscall/sysretq`), kernel-thread switch |
| `drivers/uart.cpp` | COM1 serial; mirrors output to the framebuffer console |
| `drivers/fb.cpp` | Bochs/QEMU VBE linear framebuffer found over PCI (1024x768x32) |
| `drivers/console*.cpp` | framebuffer text terminal (shared with the AArch64 tree) |
| `drivers/keyboard.cpp` | PS/2 keyboard, IntelliMouse wheel, PageUp/PageDown scrolling |
| `drivers/storage.cpp` | ATA PIO primary master |
| `drivers/virtio_net.cpp` | legacy virtio-net over PCI I/O space, polled |

## User address space

User window `0x07000000-0x07700000`, 64 KiB stack `0x077F0000-0x07800000`, guard
pages below it. Each process owns private page tables and physical frames (from
the page allocator); everything outside the window is shared with the kernel
identity map. `exec` activates the process CR3 and restores the boot CR3 when the
program returns. Segments are U/W/NX per 4 KiB page; W+X is refused.

## Run modes

- `linux/run.sh` - serial shell in the host terminal (`-nographic -vga none`).
- `linux/run-terminal.sh` - shell in a QEMU window (`-vga std`, GTK `grab-on-hover=on`).

## Verification status

Host tests (all `tests/*_host_test.cpp`) and `tests/verify_x86_64.py` run in
`linux/build.sh`. The kernel was booted and exercised on a purpose-written
x86-64 emulator (shell, file commands, `nano`, `calc`, ring 3 faults, per-process
page tables, timer IRQ in ring 3, networking, scrollback by PageUp/PageDown and
IntelliMouse wheel). Test with real QEMU: `bash linux/run-terminal.sh`.
