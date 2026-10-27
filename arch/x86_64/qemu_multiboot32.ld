OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(qemu_multiboot_entry)

/* Keep the wrapper and its embedded kernel well above the kernel's 1 MiB
 * destination. QEMU only needs to load this ELF32 Multiboot image. */
SECTIONS
{
    . = 0x01000000;
    .multiboot ALIGN(4) : { KEEP(*(.multiboot)) }
    .text ALIGN(16) : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    .rodata ALIGN(16) : { *(.rodata .rodata.*) }
    .kernel_payload ALIGN(16) : { KEEP(*(.kernel_payload)) }
    .data ALIGN(16) : { *(.data .data.*) }
    .bss ALIGN(16) (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
    }
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
