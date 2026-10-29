#!/usr/bin/env python3
"""Structural verification of the x86-64 build (not a runtime test)."""
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1]
out = root / 'build'
disk = root / 'storage/disk.img'

required = [
    'boot/boot.S', 'boot/exceptions.S', 'boot/linker.ld', 'boot/qemu_multiboot32.S', 'boot/qemu_multiboot32.ld',
    'process/user_entry.S', 'process/ctx_switch.S', 'process/user.cpp', 'process/elf.cpp',
    'process/scheduler.cpp', 'process/thread.cpp',
    'memory/mmu.cpp', 'memory/page_alloc.cpp',
    'kernel/interrupt.cpp', 'kernel/timer.cpp', 'kernel/exceptions.cpp', 'kernel/gdt.cpp',
    'kernel/fd.cpp', 'kernel/smp.cpp', 'kernel/rtc.cpp',
    'drivers/fb.cpp', 'drivers/console.cpp', 'drivers/console_font.cpp', 'drivers/uart.cpp',
    'drivers/keyboard.cpp', 'drivers/storage.cpp', 'drivers/virtio_net.cpp',
    'include/console.h', 'include/console_font.h', 'include/net_device.h', 'include/x86_io.h',
    'net/netstack.cpp', 'tools/gen_console_font.py', 'tools/make_boot_image.py',
    'tests/console_host_test.cpp', 'tests/fd_host_test.cpp', 'tests/process_host_test.cpp',
    'tests/scheduler_process_host_test.cpp', 'tests/smp_host_test.cpp',
    'docs/TERMINAL.md', 'docs/TIMER_IRQ.md', 'docs/X86_64_PORT.md', 'docs/P3_NETWORKING.md',
    'docs/P4_SMP_DRIVER_HARDENING.md', 'docs/P5_PROCESS_ISOLATION.md',
    '.github/workflows/ci.yml', 'linux/run-terminal.sh', 'windows/run-terminal.ps1',
    'README.md', '.gitignore',
]
for name in required:
    assert (root / name).is_file(), f'missing {name}'

for platform, scripts in {
    'linux': ('build.sh', 'run.sh', 'run-terminal.sh'),
    'termux': ('build.sh', 'run.sh', 'run-terminal.sh'),
    'macos': ('build.sh', 'run.sh', 'run-terminal.sh'),
    'windows': ('build.ps1', 'run.ps1', 'run-terminal.ps1'),
}.items():
    for script in scripts:
        assert (root / platform / script).is_file(), f'missing platform entry point: {platform}/{script}'
    ui = 'run-ui.ps1' if platform == 'windows' else 'run-ui.sh'
    assert not (root / platform / ui).exists(), f'GUI runner must not exist: {platform}/{ui}'
for gone in ('ui', 'apps', 'assets', 'arch'):
    assert not (root / gone).exists(), f'folder must not exist: {gone}/'
# No AArch64-only implementation may remain.
for gone in ('drivers/virtio_mmio.cpp', 'drivers/virtio_blk.cpp', 'drivers/virtio_input.cpp',
             'include/virtio_mmio.h', 'include/virtio_net.h', 'userspace/nano.S'):
    assert not (root / gone).exists(), f'AArch64-only file remains: {gone}'

kernel = out / 'kernel.elf'; wrapper = out / 'qemu-boot.elf'; flat = out / 'kernel.bin'
for p in (kernel, wrapper, flat, disk):
    assert p.is_file() and p.stat().st_size > 0, f'missing/empty artifact: {p}'
kb = kernel.read_bytes()
assert kb[:4] == b'\x7fELF' and kb[4] == 2 and kb[5] == 1, 'kernel must be ELF64 LE'
assert struct.unpack_from('<H', kb, 18)[0] == 62, 'kernel machine must be x86-64'
entry = struct.unpack_from('<Q', kb, 24)[0]
assert 0x100000 <= entry < 0x100000000, f'unexpected entry 0x{entry:x}'
wb = wrapper.read_bytes()
assert wb[:4] == b'\x7fELF' and wb[4] == 1 and wb[5] == 1 and struct.unpack_from('<H', wb, 18)[0] == 3
assert b'\x02\xb0\xad\x1b' in wb[:8192], 'Multiboot v1 header missing'
assert disk.stat().st_size == 32768 * 512, 'disk image must be 16 MiB'

markers = {
    'boot/boot.S': ('long_mode_start', 'boot_pd3'),
    'drivers/keyboard.cpp': ('IntelliMouse', 'console_scroll_lines', 'console_scroll_pages'),
    'drivers/fb.cpp': ('VBE_INDEX', 'fb_buffer'),
    'drivers/uart.cpp': ('console_putc', 'uart_putc_serial_only'),
    'kernel/interrupt.cpp': ('interrupt_idle', 'x86_irq_dispatch'),
    'kernel/timer.cpp': ('pit_set_hz', 'timer_interrupt_handler'),
    'memory/mmu.cpp': ('mmu_user_space_create', 'as_pml4', 'NXE'),
    'drivers/virtio_net.cpp': ('0x1AF4', 'net_device_send'),
}
for name, ms in markers.items():
    text = (root / name).read_text()
    for m in ms:
        assert m in text, f'marker {m!r} missing in {name}'

for prog in ('user_init', 'nano', 'calc'):
    p = out / f'{prog}.elf'
    assert p.is_file(), f'missing {p}'
    b = p.read_bytes()
    assert b[:4] == b'\x7fELF' and b[4] == 2 and struct.unpack_from('<H', b, 18)[0] == 62
    e = struct.unpack_from('<Q', b, 24)[0]
    assert 0x07000000 <= e < 0x07700000, f'{prog} entry outside user window'
    phoff = struct.unpack_from('<Q', b, 32)[0]
    phentsize, phnum = struct.unpack_from('<HH', b, 54)
    ranges = []
    entry_exec = False
    for i in range(phnum):
        o = phoff + i * phentsize
        pt, pf = struct.unpack_from('<II', b, o)
        if pt != 1:
            continue
        va = struct.unpack_from('<Q', b, o + 16)[0]
        fs, ms = struct.unpack_from('<QQ', b, o + 32)
        assert fs <= ms and (va & 0xFFF) == 0 and va + ms <= 0x07700000
        assert not (pf & 1 and pf & 2), f'{prog}: W+X segment'
        ranges.append((va, va + ms))
        if pf & 1 and va <= e < va + ms:
            entry_exec = True
    assert entry_exec, f'{prog}: entry not executable'
    for i, (a0, a1) in enumerate(ranges):
        for j, (b0, b1) in enumerate(ranges):
            assert i >= j or not (a0 < b1 and b0 < a1), f'{prog}: overlapping segments'

data = disk.read_bytes()
for n in (b'init.elf', b'nano.elf', b'calc.elf'):
    assert n in data, f'{n.decode()} missing from disk image'
for stale in (b'terminal.rpk', b'launcher.rpk', b'wallpaper.rpk'):
    assert stale not in data, f'GUI package in disk image: {stale.decode()}'

print('x86-64 structural checks passed')
