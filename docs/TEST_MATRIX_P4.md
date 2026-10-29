# RioOS 1.10.0 — P4 test matrix

> **v1.13.0 note:** the timer IRQ is now active (100 Hz tick, also while ring 3 programs run) and the GUI was removed. Statements below that say the timer IRQ is disabled/off describe the earlier version this document was written for; see [TIMER_IRQ.md](./TIMER_IRQ.md) and [TERMINAL.md](./TERMINAL.md).


| Area | Test | Expected result |
|---|---|---|
| SMP default | `tests/smp_host_test.cpp` | One online CPU, SMP disabled, CPU ID masking contract, try-lock/lock/unlock pass |
| Framebuffer | `tests/fb_host_test.cpp` | Host attach bounds, safe uninitialized state, glyph drawing, background clearing, scrolling, cursor clamp pass |
| virtio-net RX | `tests/verify_x86_64.py` | Driver must reject RX lengths above the backing buffer and reject packet truncation |
| Build | `bash linux/build.sh --clean` | x86-64 kernel and userspace ELF link successfully |
| Regression | Host suite in `linux/build.sh` | All 13 host test executables pass |
| Release | `tests/verify_x86_64.py` | ELF, expected source, syscall, networking, SMP, framebuffer and timer-off invariants pass |
| Runtime | `./linux/run.sh` on a host with QEMU x86-64 | Manual follow-up: shell, keyboard input, `smp`, `fbinfo`, disk, and networking remain stable |

The runtime row is not marked passed by the host-only build. SMP remains disabled and timer IRQ remains off by default.
