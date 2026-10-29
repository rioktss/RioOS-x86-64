#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

VERBOSE=0
RUN_QEMU=0
CLEAN=0

for arg in "$@"; do
    case "$arg" in
        -v|--verbose) VERBOSE=1 ;;
        --run) RUN_QEMU=1 ;;
        --clean) CLEAN=1 ;;
        -h|--help)
            cat <<EOF
Usage: bash ${BASH_SOURCE[0]} [options]
  --run      Build + tests + start the serial QEMU runner
  -v, --verbose  Show detailed compiler output
  --clean    Remove generated build/host-test outputs
EOF
            exit 0
            ;;
        *) echo "ERROR: unknown argument: $arg" >&2; exit 2 ;;
    esac
done

# cuma print kalo verbose
vlog() { [[ $VERBOSE -eq 1 ]] && printf '%s\n' "$*" || true; }
# step jadi diam kalo gak verbose
step() { [[ $VERBOSE -eq 1 ]] && printf '[%s] %s\n' "$1" "$2" || true; }

CLANG="${CLANG:-clang}"
CXX="${CXX:-clang++}"
LD="${LD:-ld.lld}"
READELF="${READELF:-llvm-readelf}"
PYTHON="${PYTHON:-}"

find_python() {
    if [[ -n "$PYTHON" ]] && command -v "$PYTHON" >/dev/null 2>&1; then
        printf '%s' "$PYTHON"
        return
    fi
    for p in python3 python; do
        if command -v "$p" >/dev/null 2>&1; then
            printf '%s' "$p"
            return
        fi
    done
    return 1
}

PYTHON="$(find_python || true)"

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERROR: required command not found: $1" >&2
        exit 1
    }
}

require_cmd "$CLANG"
require_cmd "$CXX"
require_cmd "$LD"

if [[ -z "$PYTHON" ]]; then
    echo "ERROR: Python 3 is required for tests/verify_x86_64.py" >&2
    exit 1
fi

mkdir -p build storage storage/home
HOST_TOOLS="${MYKERNEL_TOOLS_DIR:-$HOME/.mykernel-tools}"
mkdir -p "$HOST_TOOLS"

if [[ "$CLEAN" -eq 1 ]]; then
    rm -f build/*.o build/kernel.elf build/kernel.bin build/kernel.entry build/qemu-boot.elf build/user_init.elf build/nano.elf build/calc.elf
    rm -f "$HOST_TOOLS"/fs_host_test
    rm -f "$HOST_TOOLS"/net_host_test
    rm -f "$HOST_TOOLS"/mem_host_test
    rm -f "$HOST_TOOLS"/syscall_host_test
    rm -f "$HOST_TOOLS"/diag_host_test
    rm -f "$HOST_TOOLS"/vfs_host_test
    rm -f "$HOST_TOOLS"/env_host_test
    rm -f "$HOST_TOOLS"/fd_host_test
    rm -f "$HOST_TOOLS"/process_host_test
    rm -f "$HOST_TOOLS"/scheduler_process_host_test
    rm -f "$HOST_TOOLS"/smp_host_test "$HOST_TOOLS"/console_host_test
fi

src_of() {
    local f="$1" d
    for d in kernel drivers memory process fs net; do
        [[ -f "$d/$f.cpp" ]] && { printf '%s' "$d/$f.cpp"; return; }
    done
    echo "ERROR: source for $f not found" >&2
    exit 1
}

CXXFLAGS=(
    -Iinclude
    --target=x86_64-unknown-none-elf -DMYKERNEL_X86_64=1
    -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-builtin
    -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=small
    -nostdinc++ -std=c++17 -O1 -Wall -Wextra -Werror
)
ASMFLAGS=(
    -Iinclude
    --target=x86_64-unknown-none-elf -ffreestanding -fno-pic -fno-pie -mno-red-zone
)

MODULES=(
    uart memory string fs vfs process sched_policy scheduler timer interrupt keyboard
    commands version shell history env rtc exceptions syscall user mmu storage thread fd
    fb console console_font
    elf virtio_net netstack page_alloc fault diag security smp gdt runtime
    kernel
)

# MINIMAL PRINT CUMA INI
printf 'RioOS build...\n'

step "1/9" "Bundling userspace (init + nano + calc)"
"$CLANG" "${ASMFLAGS[@]}" -c userspace/user_init.S -o build/user_init.o
"$LD" -T userspace/user_init.ld -nostdlib -z max-page-size=0x1000 \
    build/user_init.o -o build/user_init.elf

USERFLAGS=(
    -Iinclude
    --target=x86_64-unknown-none-elf -DMYKERNEL_X86_64=1
    -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-builtin
    -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables
    -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=small
    -nostdinc++ -nostdlib -std=c++17 -O1 -Wall -Wextra -Werror
)
for prog in nano calc; do
    "$CXX" "${USERFLAGS[@]}" -c "userspace/$prog.cpp" -o "build/$prog.o"
    "$LD" -T userspace/user_init.ld -nostdlib -z max-page-size=0x1000 \
        "build/$prog.o" -o "build/$prog.elf"
    vlog " -> build/$prog.elf ready"
done

step "2/9" "Building host tools"
"$CXX" -std=c++17 -O2 -Wall -Wextra -pedantic \
    fs/mkfs_myfs.cpp -o "$HOST_TOOLS/mkfs_myfs"

step "3/9" "Preparing disk image"
if [[ ! -f storage/disk.img ]]; then
    dd if=/dev/zero of=storage/disk.img bs=512 count=32768 2>/dev/null
fi

APP_ELFS=(build/user_init.elf)
[[ -f build/nano.elf ]] && APP_ELFS+=(build/nano.elf)
[[ -f build/calc.elf ]] && APP_ELFS+=(build/calc.elf)
"$HOST_TOOLS/mkfs_myfs" storage/disk.img "${APP_ELFS[@]}" >/dev/null
vlog " -> storage/disk.img ready"

step "4/9" "Compiling boot"
"$CLANG" "${ASMFLAGS[@]}" -c boot/boot.S -o build/boot.o
"$CLANG" "${ASMFLAGS[@]}" -c boot/exceptions.S -o build/exception_vectors.o
"$CLANG" "${ASMFLAGS[@]}" -c process/user_entry.S -o build/user_entry.o
"$CLANG" "${ASMFLAGS[@]}" -c process/ctx_switch.S -o build/ctx_switch.o

step "5/9" "Compiling kernel (${#MODULES[@]} modules)"
for f in "${MODULES[@]}"; do
    if [[ "$VERBOSE" -eq 1 ]]; then
        echo " -> $f.cpp"
        "$CXX" "${CXXFLAGS[@]}" -c "$(src_of "$f")" -o "build/$f.o"
    else
        if ! "$CXX" "${CXXFLAGS[@]}" -c "$(src_of "$f")" -o "build/$f.o"; then
            echo
            echo "ERROR: gagal compile $f.cpp" >&2
            exit 1
        fi
    fi
done

step "6/9" "Linking"
OBJS=(build/boot.o build/exception_vectors.o build/user_entry.o build/ctx_switch.o)
for f in "${MODULES[@]}"; do OBJS+=("build/$f.o"); done
"$LD" -T boot/linker.ld -nostdlib -z max-page-size=0x1000 "${OBJS[@]}" -o build/kernel.elf

# Flat load image at 1 MiB, wrapped in an ELF32 Multiboot v1 shim (QEMU -kernel).
"$PYTHON" tools/make_boot_image.py build/kernel.elf build/kernel.bin build/kernel.entry
KERNEL_ENTRY="$(cat build/kernel.entry)"
"$CLANG" --target=i386-unknown-none-elf -m32 -ffreestanding -fno-pic -fno-pie \
    -DKERNEL_ENTRY_ADDRESS="$KERNEL_ENTRY" -c boot/qemu_multiboot32.S -o build/qemu_multiboot32.o
"$LD" -m elf_i386 -T boot/qemu_multiboot32.ld -nostdlib \
    build/qemu_multiboot32.o -o build/qemu-boot.elf

step "7/9" "Verifying ELF"
if [[ "$VERBOSE" -eq 1 ]]; then
    if command -v "$READELF" >/dev/null 2>&1; then
        "$READELF" -h build/kernel.elf | grep -E 'Class:|Machine:|Entry point' || true
    fi
fi

step "8/9" "Running host tests"
"$CXX" -std=c++17 -O2 -Wall -Wextra -pedantic -iquote include -DHOST_TEST \
    tests/fs_host_test.cpp fs/fs.cpp kernel/string.cpp kernel/rtc.cpp \
    -o "$HOST_TOOLS/fs_host_test"
"$HOST_TOOLS/fs_host_test" > /dev/null

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote include -DHOST_TEST \
    tests/net_host_test.cpp net/netstack.cpp \
    -o "$HOST_TOOLS/net_host_test"
"$HOST_TOOLS/net_host_test" > /dev/null

"$CXX" -std=c++17 -O2 -Wall -Wextra -pedantic -iquote include -DHOST_TEST \
    tests/mem_host_test.cpp memory/page_alloc.cpp kernel/fault.cpp \
    -o "$HOST_TOOLS/mem_host_test"
"$HOST_TOOLS/mem_host_test" > /dev/null

"$CXX" -std=c++17 -O2 -Wall -Wextra -pedantic -iquote include -DHOST_TEST \
    tests/syscall_host_test.cpp kernel/syscall.cpp kernel/fd.cpp \
    -o "$HOST_TOOLS/syscall_host_test"
"$HOST_TOOLS/syscall_host_test" > /dev/null

"$CXX" -std=c++17 -O2 -Wall -Wextra -pedantic -iquote include -DHOST_TEST \
    tests/diag_host_test.cpp kernel/diag.cpp \
    -o "$HOST_TOOLS/diag_host_test"
"$HOST_TOOLS/diag_host_test" > /dev/null

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote include -DHOST_TEST \
    tests/vfs_host_test.cpp fs/vfs.cpp fs/fs.cpp kernel/string.cpp kernel/rtc.cpp \
    -o "$HOST_TOOLS/vfs_host_test"
"$HOST_TOOLS/vfs_host_test" > /dev/null

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote include -DHOST_TEST \
    tests/sched_host_test.cpp process/sched_policy.cpp \
    -o "$HOST_TOOLS/sched_host_test"
"$HOST_TOOLS/sched_host_test" > /dev/null

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote include -DHOST_TEST \
    tests/env_host_test.cpp kernel/env.cpp kernel/string.cpp \
    -o "$HOST_TOOLS/env_host_test"
"$HOST_TOOLS/env_host_test" > /dev/null

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote include -DHOST_TEST \
    tests/fd_host_test.cpp kernel/fd.cpp fs/fs.cpp kernel/string.cpp kernel/rtc.cpp \
    -o "$HOST_TOOLS/fd_host_test"
"$HOST_TOOLS/fd_host_test" > /dev/null

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote include -DHOST_TEST \
    tests/process_host_test.cpp process/process.cpp kernel/string.cpp \
    -o "$HOST_TOOLS/process_host_test"
"$HOST_TOOLS/process_host_test" > /dev/null

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote include -DHOST_TEST \
    tests/scheduler_process_host_test.cpp process/scheduler.cpp process/process.cpp kernel/string.cpp \
    -o "$HOST_TOOLS/scheduler_process_host_test"
"$HOST_TOOLS/scheduler_process_host_test" > /dev/null

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote include -DHOST_TEST \
    tests/smp_host_test.cpp kernel/smp.cpp \
    -o "$HOST_TOOLS/smp_host_test"
"$HOST_TOOLS/smp_host_test" > /dev/null

"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -iquote include -DHOST_TEST \
    tests/console_host_test.cpp drivers/console.cpp drivers/console_font.cpp drivers/fb.cpp \
    -o "$HOST_TOOLS/console_host_test"
"$HOST_TOOLS/console_host_test" > /dev/null


# MINIMAL SUCCESS
printf 'build success\n'

if [[ "$RUN_QEMU" -eq 1 ]]; then
    if command -v "${QEMU:-qemu-system-x86_64}" >/dev/null 2>&1; then
        exec "$SCRIPT_DIR/run.sh"
    else
        echo "ERROR: QEMU not found." >&2
        exit 1
    fi
fi