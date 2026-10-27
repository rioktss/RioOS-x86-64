/*
 * QEMU direct -kernel loader shim for a 64-bit RioOS kernel.
 *
 * QEMU's PC direct Multiboot loader requires a 32-bit ELF image on builds
 * which reject an ELF64 Multiboot kernel. This tiny ELF32 image is loaded by
 * QEMU, copies an embedded flat image of the real x86-64 kernel to 1 MiB,
 * then jumps to that kernel's 32-bit bootstrap. The kernel bootstrap itself
 * enables long mode and enters the 64-bit C++ kernel.
 *
 * This is NOT a GRUB ISO and does not require a guest bootloader or disk image.
 */
.section .multiboot, "a"
.align 4
.long 0x1BADB002
.long 0x00000003
.long -(0x1BADB002 + 0x00000003)

.section .text.boot, "ax"
.code32
.global qemu_multiboot_entry
.type qemu_multiboot_entry, @function
.extern __kernel_payload_start
.extern __kernel_payload_end

qemu_multiboot_entry:
    cli
    cld

    /* A scratch stack in conventional RAM; this shim makes no calls. */
    movl $0x00070000, %esp

    /* Copy the embedded, file-backed kernel image from the shim to 1 MiB. */
    movl $__kernel_payload_start, %esi
    movl $0x00100000, %edi
    movl $(__kernel_payload_end - __kernel_payload_start), %ecx
    rep movsb

    /* The target's linker script places _start at the ELF entry address. */
    movl $KERNEL_ENTRY_ADDRESS, %eax
    jmp *%eax

1:
    cli
    hlt
    jmp 1b

.section .kernel_payload, "a"
.balign 16
.global __kernel_payload_start
__kernel_payload_start:
.incbin "build-x86_64/kernel.bin"
.global __kernel_payload_end
__kernel_payload_end:
