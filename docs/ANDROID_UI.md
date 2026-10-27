# RioOS Android-like UI

RioOS includes a small bare-metal UI built from the shared C++ renderer and app modules. On this x86-64 target it uses QEMU standard VGA / Bochs VBE and a linear framebuffer; no SDL, Qt, LVGL, or desktop GUI library is linked into the guest kernel. The serial console retains a fixed-cell bitmap font while GUI labels use a compact antialiased proportional glyph table derived from Inter.

## Features

- Boot splash, lock screen, four-column Home grid, status bar, and dock.
- Press `C` to open Control Centre; keyboard navigation is the supported x86 input path.
- Quick actions for network status, dark/light theme, Wallpaper, Settings, locking, and closing the panel.
- `wallpaper.rpk` selects local images from `/wallpapers`; the choice is saved in `/settings/wallpaper.cfg`.
- `pengaturan.rpk` opens appearance settings and stores the selected theme in `/settings/theme.cfg`.
- RPK manifests can reference local icon images with `icon=/icons/<name>.bmp`.

## Local wallpaper and icon files

Supported image format is uncompressed BMP (24-bit or 32-bit), maximum file size **1 MiB**. To bundle another wallpaper, copy it to `assets/wallpapers/` and rebuild:

```sh
cp ~/Pictures/my-wallpaper.bmp assets/wallpapers/
bash linux/build-x86_64.sh
```

Replace app icons under `assets/icons/` and rebuild. The build seeds all valid assets and app manifests into the generated MyFS disk image. If an image is missing or malformed, the UI falls back to its built-in icon or default wallpaper.

The `.rpk` files use the `RIOPKG/1` manifest format and select built-in kernel app handlers. They are metadata, not dynamically loaded executable packages.

## Input and launch

```sh
bash linux/run-ui.sh
# or, if QEMU has SDL but not GTK:
RIOOS_DISPLAY=sdl bash linux/run-ui.sh
```

The x86-64 port polls PS/2 keyboard and mouse input. Use arrows or WASD and Enter to navigate; where the QEMU mouse is available, use the on-screen cursor to interact. If a supported VBE framebuffer cannot be enabled, run `bash linux/run.sh` for the serial shell. This package has been compile-checked, but full QEMU display and input behavior still needs runtime verification on the target host.

Inter font licensing and attribution are included in `THIRD_PARTY_NOTICES.md`; the original font file is not bundled.
