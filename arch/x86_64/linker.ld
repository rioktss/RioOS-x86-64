ENTRY(_start)
__kernel_entry_address = _start;
SECTIONS
{
    . = 0x00100000;
    __kernel_start = .;
    .multiboot ALIGN(4) : { KEEP(*(.multiboot)) }
    .text ALIGN(0x1000) : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    .rodata ALIGN(0x1000) : { *(.rodata .rodata.*) }
    .data ALIGN(0x1000) : { *(.data .data.*) }
    .bss ALIGN(0x1000) (NOLOAD) : {
        __bss_start = .;
        *(.bss.boot_tables)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(0x1000);
        __bss_end = .;
    }
    __kernel_end = .;
    __heap_start = ALIGN(., 16);
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
