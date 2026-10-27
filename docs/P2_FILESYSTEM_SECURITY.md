# Filesystem and security notes

The common MyFS/VFS implementation includes path resolution, file/directory ownership metadata, permission checks, bounded read/write, `stat`, truncate, and `chmod` APIs. The kernel has a shell-level root/user role and a simple development authentication flow. This is an educational model rather than production security: the development root password is embedded in source and credentials are not a complete per-process isolation model.

The shared syscall dispatcher contains permission-aware filesystem operations and temporary unprivileged credentials for user-mode requests. On the x86-64 port these paths are reachable from ring-3 executables (`exec`, `nano`, `calc`) through native `syscall` entry/return and per-program user page tables.

## Known gaps

- No journaling or crash-atomic multi-inode transactions.
- No symlink support or full POSIX permission model.
- No independent per-process address spaces or concurrent user processes.
- No claim of durability until ATA disk writes and reboot persistence have been tested in QEMU on the target host.

## Validation

```sh
bash linux/build-x86_64.sh --clean
python3 tests/verify_x86_64.py
```

The verifier checks compilation artifacts only. Run QEMU, create a file, reboot, and confirm disk persistence before treating the hardware storage path as runtime-verified.
