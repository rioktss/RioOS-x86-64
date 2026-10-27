# RioOS Pixel UI

RioOS v1.11.0 includes a pixel-rendered UI with a boot splash, lock screen, home launcher, Control Centre, and built-in Terminal, Calculator, Clock, Files, Launcher, Wallpaper, and Settings apps. The renderer is freestanding C++ with direct framebuffer primitives and a compact antialiased proportional font; the serial console keeps the legacy bitmap font. App implementations are statically linked into the kernel.

## Build and launch

```sh
bash linux/build-x86_64.sh --clean
bash linux/run-ui.sh
```

For an SDL-enabled QEMU build, use `RIOOS_DISPLAY=sdl bash linux/run-ui.sh`. The runner requests a 1024×768×32 linear framebuffer from QEMU standard VGA / Bochs VBE. If no compatible VGA LFB is found, the kernel reports the condition over serial and leaves the serial shell available.

## Keyboard controls

| Screen | Keys | Action |
|---|---|---|
| Lock | Any key / Enter | Unlock |
| Home | A/D, W/S, arrows, Enter, `1`–`7` | Navigate and open an app |
| Home | `C` | Open Control Centre |
| Control Centre | `1`–`6`, `C`, `Esc` | Use quick actions or close panel |
| Home | `K` | Lock screen |
| Home | `X` | Return to serial shell |
| Apps | `Esc` | Back/home |
| Terminal | Type command, Enter, Up/Down | Run RioOS shell commands and browse history |
| Wallpaper | W/S, Enter | Select and persist wallpaper |
| Settings | T/W | Change theme or open wallpaper selection |
| Files / Launcher | W/S, Enter, Backspace | Navigate items and directories |

The terminal app calls the same `execute_command()` dispatcher as the serial shell; it is not GNU Bash. On x86-64 the PS/2 mouse drives the pointer and a software cursor is drawn. Host display/input support varies with QEMU's GTK/SDL build.

## RPK manifests and images

The build installs these manifests under `/apps`: `terminal.rpk`, `calculator.rpk`, `clock.rpk`, `files.rpk`, `launcher.rpk`, `wallpaper.rpk`, and `pengaturan.rpk`. Add wallpapers to `assets/wallpapers/` and icons to `assets/icons/`; uncompressed 24-bit/32-bit BMP up to 1 MiB per file is supported. Manifests select built-in handlers and do not represent executable binary packages.

## Test status

Compile and structural checks can be run with:

```sh
bash linux/build-x86_64.sh --clean
python3 tests/verify_x86_64.py
```

These checks do not prove a visible, interactive QEMU boot. The environment used to prepare this source package did not contain `qemu-system-x86_64`; run a real QEMU UI test on the target host before treating the framebuffer backend as runtime-verified.
