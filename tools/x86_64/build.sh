#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$PROJECT_ROOT"

VERBOSE=0
CLEAN=0
RUN_QEMU=0
SKIP_TESTS=0
for arg in "$@"; do
    case "$arg" in
        --skip-tests) SKIP_TESTS=1 ;;
        -v|--verbose) VERBOSE=1 ;;
        --clean) CLEAN=1 ;;
        --run) RUN_QEMU=1 ;;
        -h|--help)
            cat <<'HELP'
Usage: bash linux/build.sh [--clean] [--verbose] [--skip-tests] [--run] (also available under termux/ and macos/)
Build the freestanding MyKernel x86-64 kernel for QEMU PC.
Options: --clean removes generated x86-64 output; --skip-tests skips host unit tests; --run boots QEMU after build.
HELP
            exit 0 ;;
        *) echo "ERROR: unknown argument: $arg" >&2; exit 2 ;;
    esac
done

CLANG="${CLANG:-clang}"
CXX="${CXX:-clang++}"
LD="${LD:-ld.lld}"
PYTHON="${PYTHON:-python3}"
OUT=build-x86_64
TOOLS="${MYKERNEL_TOOLS_DIR:-$HOME/.mykernel-tools}"
mkdir -p "$OUT" "$TOOLS" storage
if [[ "$CLEAN" -eq 1 ]]; then rm -rf "$OUT"; mkdir -p "$OUT"; fi
for tool in "$CLANG" "$CXX" "$LD" "$PYTHON"; do
    command -v "$tool" >/dev/null 2>&1 || { echo "ERROR: required tool not found: $tool" >&2; exit 1; }
done

# Userspace programs (ring 3): init self-test, nano, calc.
USERFLAGS=(--target=x86_64-unknown-none-elf -Iinclude -DMYKERNEL_X86_64=1
    -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-builtin
    -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=small
    -nostdinc++ -nostdlib -std=c++17 -O1 -Wall -Wextra -Werror)
mkdir -p "$OUT/user"
"$CLANG" --target=x86_64-unknown-none-elf -ffreestanding -fno-pic -fno-pie -c userspace/user_init.S -o "$OUT/user/user_init.o"
"$LD" -T userspace/user_init.ld -nostdlib -z max-page-size=0x1000 "$OUT/user/user_init.o" -o "$OUT/user/user_init.elf"
for prog in nano calc; do
    "$CXX" "${USERFLAGS[@]}" -c "userspace/$prog.cpp" -o "$OUT/user/$prog.o"
    "$LD" -T userspace/user_init.ld -nostdlib -z max-page-size=0x1000 "$OUT/user/$prog.o" -o "$OUT/user/$prog.elf"
done

# Create/update the persistent MyFS volume and seed it with built-in resources.
mkdir -p storage storage/home
"$CXX" -std=c++17 -O2 -Wall -Wextra -pedantic fs/mkfs_myfs.cpp -o "$TOOLS/mkfs_myfs"
if [[ ! -f storage/disk.img ]]; then
    "$PYTHON" -c 'from pathlib import Path; p=Path("storage/disk.img"); f=p.open("wb"); f.truncate(16 * 1024 * 1024); f.close()'
fi
SEED_FILES=("$OUT/user/user_init.elf" "$OUT/user/nano.elf" "$OUT/user/calc.elf" apps/terminal.rpk apps/calculator.rpk apps/clock.rpk apps/files.rpk apps/launcher.rpk apps/wallpaper.rpk apps/pengaturan.rpk)
for asset in assets/wallpapers/*.bmp assets/icons/*.bmp; do [[ -f "$asset" ]] && SEED_FILES+=("$asset"); done
"$TOOLS/mkfs_myfs" storage/disk.img "${SEED_FILES[@]}" >/dev/null

CXXFLAGS=(--target=x86_64-unknown-none-elf -Iinclude -DMYKERNEL_X86_64=1
    -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-builtin
    -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=small -nostdinc++ -std=c++17 -O1 -Wall -Wextra -Werror)
ASMFLAGS=(--target=x86_64-unknown-none-elf -Iinclude -ffreestanding -fno-pic -fno-pie -mno-red-zone)
log() { [[ "$VERBOSE" -eq 1 ]] && printf '%s\n' "$*" || true; }
compile_cpp() {
    local src="$1" out="$2"
    log "CXX $src"
    "$CXX" "${CXXFLAGS[@]}" -c "$src" -o "$OUT/$out.o"
}
compile_asm() {
    local src="$1" out="$2"
    log "AS  $src"
    "$CLANG" "${ASMFLAGS[@]}" -c "$src" -o "$OUT/$out.o"
}

printf 'MyKernel x86-64 build...\n'

# Architecture-specific low-level implementation.
compile_asm arch/x86_64/boot.S arch_x86_64_boot.S
compile_asm arch/x86_64/ctx_switch.S arch_x86_64_ctx_switch.S
compile_asm arch/x86_64/exceptions.S arch_x86_64_exceptions.S
compile_asm arch/x86_64/user_entry.S arch_x86_64_user_entry.S

SOURCES=(
    kernel/commands.cpp kernel/diag.cpp kernel/env.cpp kernel/fault.cpp kernel/fd.cpp
    kernel/history.cpp kernel/kernel.cpp kernel/security.cpp kernel/shell.cpp
    kernel/string.cpp kernel/syscall.cpp kernel/version.cpp
    memory/memory.cpp memory/page_alloc.cpp
    fs/fs.cpp fs/vfs.cpp net/netstack.cpp
    process/elf.cpp process/process.cpp process/sched_policy.cpp process/scheduler.cpp
    process/thread.cpp process/user.cpp
    drivers/font.cpp drivers/ui_font_data.cpp
    ui/apps.cpp ui/boot.cpp ui/calculator.cpp ui/clock.cpp ui/file_manager.cpp
    ui/home_screen.cpp ui/image.cpp ui/launcher.cpp ui/lock_screen.cpp
    ui/settings.cpp ui/terminal.cpp ui/ui.cpp ui/wallpaper.cpp
    arch/x86_64/exceptions.cpp arch/x86_64/gdt.cpp arch/x86_64/fb.cpp arch/x86_64/interrupt.cpp
    arch/x86_64/mmu.cpp arch/x86_64/net.cpp arch/x86_64/platform.cpp arch/x86_64/runtime.cpp
    arch/x86_64/smp.cpp arch/x86_64/timer.cpp arch/x86_64/uart.cpp
)
OBJECTS=(arch_x86_64_boot.S.o arch_x86_64_ctx_switch.S.o arch_x86_64_exceptions.S.o arch_x86_64_user_entry.S.o)
for src in "${SOURCES[@]}"; do
    obj="${src//\//_}"
    obj="${obj%.cpp}"
    compile_cpp "$src" "$obj"
    OBJECTS+=("$obj.o")
done

printf 'Linking kernel ELF...\n'
"$LD" -T arch/x86_64/linker.ld -nostdlib -z max-page-size=0x1000 \
    "${OBJECTS[@]/#/$OUT/}" -o "$OUT/kernel.elf"

# Produce the flat load image needed by QEMU's 32-bit Multiboot direct loader.
# ELF PT_LOAD segments are flattened at physical address 1 MiB; .bss is zeroed by boot.S.
"$PYTHON" - "$OUT/kernel.elf" "$OUT/kernel.bin" <<'PY'
import struct, sys
src, dst = sys.argv[1:]
data = open(src, 'rb').read()
if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
    raise SystemExit('ERROR: kernel.elf is not a little-endian ELF64 file')
phoff = struct.unpack_from('<Q', data, 32)[0]
phentsize = struct.unpack_from('<H', data, 54)[0]
phnum = struct.unpack_from('<H', data, 56)[0]
loads = []
for i in range(phnum):
    o = phoff + i * phentsize
    p_type, p_flags = struct.unpack_from('<II', data, o)
    if p_type != 1: continue
    p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack_from('<QQQQQQ', data, o + 8)
    if p_filesz:
        loads.append((p_paddr or p_vaddr, p_offset, p_filesz))
if not loads:
    raise SystemExit('ERROR: no file-backed PT_LOAD segments found')
base = min(x[0] for x in loads)
end = max(addr + size for addr, off, size in loads)
if base != 0x100000:
    raise SystemExit(f'ERROR: expected kernel payload at 0x100000, got 0x{base:x}')
out = bytearray(end - base)
for addr, off, size in loads:
    out[addr-base:addr-base+size] = data[off:off+size]
open(dst, 'wb').write(out)
print(f'ELF payload: base=0x{base:x}, file bytes={len(out)}')
PY

# QEMU's direct PC multiboot loader wants ELF32; this tiny shim embeds kernel.bin.
KERNEL_ENTRY="$("$PYTHON" - "$OUT/kernel.elf" <<'PYENTRY'
import struct, sys
b = open(sys.argv[1], 'rb').read()
print('0x%x' % struct.unpack_from('<Q', b, 24)[0])
PYENTRY
)"
"$CLANG" --target=i386-unknown-none-elf -m32 -ffreestanding -fno-pic -fno-pie \
    -DKERNEL_ENTRY_ADDRESS="$KERNEL_ENTRY" -c arch/x86_64/qemu_multiboot32.S -o "$OUT/qemu_multiboot32.o"
"$LD" -m elf_i386 -T arch/x86_64/qemu_multiboot32.ld -nostdlib \
    "$OUT/qemu_multiboot32.o" -o "$OUT/qemu-boot.elf"

# Sanity-check architecture, ELF class, entry point, and Multiboot marker.
"$PYTHON" - "$OUT/kernel.elf" "$OUT/qemu-boot.elf" <<'PY'
import struct, sys
for path, expected_class, machine in ((sys.argv[1], 2, 62), (sys.argv[2], 1, 3)):
    b=open(path,'rb').read()
    if b[:4] != b'\x7fELF' or b[4] != expected_class or b[5] != 1:
        raise SystemExit('ERROR: invalid ELF header: '+path)
    got=struct.unpack_from('<H',b,18)[0]
    if got != machine: raise SystemExit(f'ERROR: unexpected ELF machine {got} in {path}')
wrapper=open(sys.argv[2],'rb').read()
if b'\x02\xb0\xad\x1b' not in wrapper[:8192]:
    raise SystemExit('ERROR: missing Multiboot v1 header in QEMU wrapper')
print('ELF and Multiboot checks passed.')
PY

# Host unit tests (native, -DHOST_TEST) and structural release verification.
if [[ "$SKIP_TESTS" -eq 0 ]]; then
    printf 'Running host tests...\n'
    mkdir -p build "$TOOLS"
    HOST_FLAGS=(-std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote include -DHOST_TEST)
    host_test() {
        local name="$1"; shift
        "$CXX" "${HOST_FLAGS[@]}" "tests/$name.cpp" "$@" -o "$TOOLS/$name"
        "$TOOLS/$name" >/dev/null
    }
    FB_SRC=(arch/x86_64/fb.cpp drivers/font.cpp drivers/ui_font_data.cpp)
    host_test fs_host_test fs/fs.cpp kernel/string.cpp kernel/rtc.cpp
    host_test net_host_test net/netstack.cpp
    host_test mem_host_test memory/page_alloc.cpp kernel/fault.cpp
    host_test syscall_host_test kernel/syscall.cpp kernel/fd.cpp
    host_test diag_host_test kernel/diag.cpp
    host_test vfs_host_test fs/vfs.cpp fs/fs.cpp kernel/string.cpp kernel/rtc.cpp
    host_test sched_host_test process/sched_policy.cpp
    host_test env_host_test kernel/env.cpp kernel/string.cpp
    host_test fd_host_test kernel/fd.cpp fs/fs.cpp kernel/string.cpp kernel/rtc.cpp
    host_test process_host_test process/process.cpp kernel/string.cpp
    host_test scheduler_process_host_test process/scheduler.cpp process/process.cpp kernel/string.cpp
    host_test smp_host_test kernel/smp.cpp
    host_test fb_host_test "${FB_SRC[@]}"
    host_test ui_host_test "${FB_SRC[@]}" ui/boot.cpp ui/lock_screen.cpp ui/home_screen.cpp
    host_test ui_image_host_test "${FB_SRC[@]}" ui/image.cpp ui/settings.cpp kernel/string.cpp
    "$PYTHON" tests/verify_x86_64.py
fi

printf 'Build OK: %s/kernel.elf and %s/qemu-boot.elf\n' "$OUT" "$OUT"
if [[ "$RUN_QEMU" -eq 1 ]]; then
    RUN_SCRIPT="${RIOOS_PLATFORM_DIR:-$PROJECT_ROOT/linux}/run.sh"
    [[ -f "$RUN_SCRIPT" ]] || { echo "ERROR: run script not found: $RUN_SCRIPT" >&2; exit 1; }
    exec bash "$RUN_SCRIPT"
fi
