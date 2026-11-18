# RioOS / MyKernel x86-64 port status

This directory is the x86-64 port of the AArch64 project. The portable shell,
filesystem, VFS, security, text, crypto and networking code has been brought
across where it can run unchanged. CPU and device code is native to x86-64;
ARM exception-level, GIC, PSCI and VirtIO-MMIO code is not reused as if it were
an x86 driver.

> **Parity note:** this is a substantial feature port, not yet a 100% feature-
> identical build. The x86-specific scheduler, address-space extensions,
> PCI storage, USB and several VirtIO device drivers still need native
> implementations and hardware/runtime validation. Unsupported operations
> return an error or report unavailable rather than pretending to work.

## Implemented and built on x86-64

- 64-bit x86 bootstrap: Multiboot v1 loader shim, long-mode entry, GDT/TSS,
  IDT and exception stubs.
- Ring 0 kernel, `SYSCALL/SYSRET` system-call entry, existing ring-3 ELF loader,
  user/kernel boundary checks and the original per-process address-space path.
- 8259 PIC + 8254 PIT system tick, CMOS RTC, interrupt control and idle/halt.
- COM1 serial console, PS/2 keyboard, QEMU/Bochs framebuffer console and
  scrollback when a compatible framebuffer is available.
- Heap and physical page allocator, kernel memory diagnostics, process table,
  cooperative kernel threads and the existing single-CPU scheduler.
- ATA PIO primary-master block access; MyFS persistent storage and filesystem
  repair; VFS. The FAT32 implementation is included, but the only ATA disk is
  the mounted root volume and is deliberately protected; there is no separate
  raw volume available until a secondary-disk driver is ported. The x86 RAM
  tmpfs block backend is also not yet implemented.
- File ownership and permissions, users/groups, password hashes, login,
  file descriptors, environment, shell history and TTY line discipline.
- Expanded shell commands, command aliases/functions, script parser, text
  utilities/filters and job-control command parsing. See the limitations below
  for execution modes that need process spawning.
- IPv4 ARP, ICMP, UDP, TCP client sockets, DNS, packet filtering/firewall rules
  and the existing legacy VirtIO-net PCI I/O transport. The current x86 setup
  uses static IPv4; DHCP is not ported.
- SHA-256, PBKDF2-HMAC-SHA256, random-generator framework, crypto, TLS/X.509
  and SSH/key code are included in the x86 build.

## Present but limited / not yet at AArch64 parity

| Area | x86-64 status |
|---|---|
| Process/task model | Existing foreground ELF execution and cooperative kernel threads work in the original x86 path. New `task_spawn`/fork/exec/signal entry points are explicit `ENOTSUP` stubs until process-per-task switching and signal delivery are ported. |
| Jobs, pipelines, background commands | Parser and job-control builtins are present, but they depend on task spawning; full asynchronous jobs and multi-process pipelines are not functional yet. |
| Memory mapping | The AArch64 mapping implementation is present in the tree for reference, but is not linked into the x86 build because x86 page-table hooks/COW/lazy-fault integration are not complete. |
| Networking servers | TCP listen/accept, DHCP client/renew/release, and HTTP/FTP server daemons are not implemented in this port. TCP client, UDP, DNS and IPv4 firewall paths are available. |
| IPv6 | Not implemented in the x86 transport yet; IPv6 entry points report unsupported. |
| PCI enumeration / AHCI / NVMe | Headers and explicit unavailable hooks are provided; native PCI enumeration and AHCI/NVMe drivers are still required. Current block I/O is ATA PIO primary master only. |
| Secondary volumes / FAT32 / tmpfs | FAT32 code is compiled, but the only raw disk is also the mounted MyFS root and is protected against raw writes. No second disk or RAM-backed tmpfs block driver is available yet. |
| USB | xHCI/USB host-controller driver is not ported. |
| VirtIO devices | Legacy PCI VirtIO-net is the active network path. VirtIO block, GPU, input, sound, RNG and 9p are not enabled by the x86 port yet. |
| 9p host filesystem | AArch64 9p filesystem code/API references are present, but no x86 VirtIO-9p transport is active. |
| SMP | x86 boot is deliberately single CPU; AP startup and inter-processor interrupts are not ported. |
| UI / audio | The framebuffer terminal is supported. AArch64 UI widget stack, image pipeline and audio playback are not ported to x86. |
| Userland | The x86 build includes the existing init, `nano` and `calc` ELF programs; the full AArch64 userspace/libc/test suite is not yet mirrored. |

## Build and run

From the project root:

```sh
bash linux/build.sh
bash linux/run.sh
```

For a graphical QEMU terminal:

```sh
bash linux/run-terminal.sh
```

The build validates the ELF64 kernel, generated flat payload, ELF32 Multiboot
wrapper and host-side regression tests. `qemu-system-x86_64` is required for
runtime boot testing. A successful compile is not a substitute for booting and
exercising it on QEMU or real hardware.

## First-boot account security

On a newly formatted disk the default accounts are `root` and `user`, and both
initially use password `123`. Change these immediately using the `passwd`
command. The account database stores salted password hashes; it does not store
plain-text passwords.
