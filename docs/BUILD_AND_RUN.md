# Build and run MyKernel x86-64

## Requirements

Build with `clang`, `clang++`, `ld.lld`, and Python 3. To start the system, install `qemu-system-x86_64`. A graphical display backend is required for the terminal-window and pixel-UI modes. Normal QEMU boot does not require GRUB or an ISO.

Check tools:

```sh
clang --version
clang++ --version
ld.lld --version
python3 --version
qemu-system-x86_64 --version
```

## Build

Choose the script for the host environment and run it from the repository root (or use the path as shown from any working directory):

```sh
bash linux/build.sh --clean --verbose
# Android/Termux: bash termux/build.sh --clean
# macOS:         bash macos/build.sh --clean
# Windows:       .\windows\build.ps1 --clean
```

Bash scripts support `--clean`, `--verbose`/`-v`, `--skip-tests`, `--run`, and `--help`. The build runs native host tests and `tests/verify_x86_64.py` by default. `--skip-tests` skips those checks. `build-x86_64.sh` remains as a compatibility alias inside the Bash platform folders.

Generated files are in `build-x86_64/`: `kernel.elf` is the x86-64 kernel, `qemu-boot.elf` is the ELF32 Multiboot wrapper used by QEMU's PC loader, `kernel.bin` is the flattened load payload, and user programs are under `build-x86_64/user/`. The first build creates and seeds `storage/disk.img` (16 MiB). That disk persists between runs; back it up before deleting or regenerating it.

## Headless serial shell

```sh
bash linux/run.sh
```

The QEMU PC machine uses one `qemu64` CPU, 128 MiB RAM by default, a COM1 serial console, an IDE disk, and PCI VirtIO networking. Override settings through environment variables, for example:

```sh
RIOOS_MEM=256M RIOOS_ACCEL=tcg bash linux/run.sh
```

## Black text-console window

```sh
bash linux/run-terminal.sh
```

This starts QEMU with VGA disabled and places the original serial shell in a separate black text-console window. It is **not** the pixel UI's Terminal app. Use `RIOOS_DISPLAY=sdl` or another backend supported by your QEMU build if the default is unavailable.

## Graphical pixel UI

```sh
bash linux/run-ui.sh
# or
RIOOS_DISPLAY=sdl bash linux/run-ui.sh
```

The UI uses QEMU standard VGA / Bochs VBE, keyboard input, and a PS/2 mouse cursor. Navigate with arrow keys, WASD, Enter/click, depending on the screen. On Linux a graphical session (`DISPLAY` or `WAYLAND_DISPLAY`) is required for GTK/SDL. macOS wrappers default to Cocoa, and Termux wrappers default to SDL.

## Optional ISO

```sh
bash linux/make-x86_64-iso.sh
```

The helper requires `grub-mkrescue` and `xorriso`. This is optional; direct QEMU boot does not require them.

## Validation

```sh
bash linux/build.sh --clean
python3 tests/verify_x86_64.py
```

The structural verifier checks x86-64 ELF headers, the i386 Multiboot wrapper, user ELFs, and the generated disk image. It does not replace a runtime boot test on the target QEMU build/device.
