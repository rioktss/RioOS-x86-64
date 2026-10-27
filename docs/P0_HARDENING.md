# Shared process and memory hardening notes

The shared source retains bounded process metadata, conservative ELF validation, denied writable/executable segment combinations, executable-file permission checks, process lifecycle checks, file-descriptor ownership, and pointer-validation interfaces.

## x86-64 limitation

This x86-64 port does **not** yet have page-table-based ring-3 mappings, a TSS-backed privilege transition, or a native syscall entry/return path. `process/user.cpp` intentionally rejects `exec` instead of attempting to run an unverified image. Therefore, shared EL0/ring-3 hardening code must be treated as porting scaffolding on this target, not as an active privilege boundary.

## Validation

```sh
bash linux/build-x86_64.sh --clean
python3 tests/verify_x86_64.py
```

The above checks compilation and image structure only. Before enabling userspace, implement and test x86-64 page tables, user/kernel privilege transitions, a native syscall ABI, page-fault reporting and return handling, and independent process address spaces. Runtime QEMU validation is separate and still required.
