# Portable build notes — x86-64

Entry points are organized by host platform. Shared build/run implementations live under `tools/x86_64/`; platform scripts set the project root and dispatch to those implementations, so the commands work from any current directory.

| Host | Build | Headless serial shell | Black text-console window | Pixel UI |
|---|---|---|---|---|
| Linux | `bash linux/build.sh` | `bash linux/run.sh` | `bash linux/run-terminal.sh` | `bash linux/run-ui.sh` |
| Termux | `bash termux/build.sh` | `bash termux/run.sh` | `bash termux/run-terminal.sh` | `bash termux/run-ui.sh` |
| macOS | `bash macos/build.sh` | `bash macos/run.sh` | `bash macos/run-terminal.sh` | `bash macos/run-ui.sh` |
| Windows PowerShell | `./windows/build.ps1` | `./windows/run.ps1` | `./windows/run-terminal.ps1` | `./windows/run-ui.ps1` |

Required build tools are `clang`, `clang++`, `ld.lld`, and Python 3. Running requires `qemu-system-x86_64`. GUI/terminal-window modes also need a QEMU display backend. On Linux use a desktop terminal; on Termux use a supported Termux:X11 setup; on macOS the wrappers default to Cocoa when that backend is available.

`--clean` removes generated x86-64 output before building. `--skip-tests` skips the host tests and structural verifier; `--verbose` shows compiler commands; `--run` starts the normal headless runner after building. Generated kernel/user ELFs are in `build-x86_64/`; `storage/disk.img` is persistent between runs. Back up the disk image before replacing it if it contains files to keep.

An optional bootable ISO can be built on Bash-capable hosts with `bash linux/make-x86_64-iso.sh` (requires `grub-mkrescue` and `xorriso`). Standard QEMU boot uses the Multiboot wrapper directly and does not require an ISO.
