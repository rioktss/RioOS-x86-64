# File descriptors and pipes

The shared kernel implements bounded per-process file descriptor tables, standard streams, open/read/write/seek/close, duplicated descriptors (`dup`/`dup2`), and bounded non-blocking byte pipes. The syscall numbers and wrappers are documented in `include/syscall.h` and `include/user_api.h`.

On this x86-64 port, these facilities are compiled into the kernel, but native ring-3 execution, user page tables, and the x86 syscall entry/return path are not finished. Therefore the EL0/ring-3 wrappers are interface scaffolding and are not currently runnable by user programs. The built-in UI Terminal uses the kernel's shared `execute_command()` dispatcher instead.

## Important current limits

- 24 descriptor slots per process and bounded open-file/pipe tables.
- Pipes are non-blocking; empty reads return an errno-like negative value, closed write endpoints return EOF, and writing without readers reports an `EPIPE`-like error.
- `fork()`, `wait()`, descriptor inheritance and concurrent user processes are not implemented.
- Timer IRQ is disabled during normal boot.

## Verification

Run `bash linux/build-x86_64.sh --clean` and `python3 tests/verify_x86_64.py`. These validate the kernel build and image structure, not active user-mode syscall behavior. Actual QEMU boot and interaction tests remain necessary.
