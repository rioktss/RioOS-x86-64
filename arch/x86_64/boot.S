/* Multiboot v1 entry: 32-bit protected mode -> 64-bit long mode. */
.section .multiboot, "a"
.align 4
.long 0x1BADB002
.long 0x00000003
.long -(0x1BADB002 + 0x00000003)

.section .text.boot, "ax"
.code32
.global _start
.type _start, @function
.extern kernel_main
.extern __bss_start
.extern __bss_end
_start:
    cli
    cld
    movl $boot_stack_top, %esp

    /* Zero .bss before using page tables/stack-backed kernel state. */
    xorl %eax, %eax
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    shrl $2, %ecx
    rep stosl

    /* Identity-map the first 4 GiB with 2 MiB pages. This includes RAM,
       legacy devices, and the QEMU std VGA linear framebuffer BAR. */
    movl $boot_pd0, %eax
    orl $0x003, %eax
    movl %eax, boot_pdpt
    movl $0, boot_pdpt+4
    movl $boot_pd1, %eax
    orl $0x003, %eax
    movl %eax, boot_pdpt+8
    movl $0, boot_pdpt+12
    movl $boot_pd2, %eax
    orl $0x003, %eax
    movl %eax, boot_pdpt+16
    movl $0, boot_pdpt+20
    movl $boot_pd3, %eax
    orl $0x003, %eax
    movl %eax, boot_pdpt+24
    movl $0, boot_pdpt+28

    /* Fill four page directories, each mapping a 1 GiB physical window. */
    movl $boot_pd0, %edi
    xorl %edx, %edx
    call fill_page_directory
    movl $boot_pd1, %edi
    movl $0x40000000, %edx
    call fill_page_directory
    movl $boot_pd2, %edi
    movl $0x80000000, %edx
    call fill_page_directory
    movl $boot_pd3, %edi
    movl $0xC0000000, %edx
    call fill_page_directory
    jmp page_directories_ready

/* 32-bit cdecl-like local helper: EDI = directory, EDX = 1 GiB base. */
fill_page_directory:
    xorl %ecx, %ecx
1:
    movl %ecx, %eax
    shll $21, %eax
    addl %edx, %eax
    orl $0x083, %eax             /* present | writable | huge-page */
    movl %eax, (%edi,%ecx,8)
    movl $0, 4(%edi,%ecx,8)
    incl %ecx
    cmpl $512, %ecx
    jne 1b
    ret
page_directories_ready:
    movl $boot_pdpt, %eax
    orl $0x003, %eax
    movl %eax, boot_pml4
    movl $0, boot_pml4+4

    /* CR4.PSE + PAE, EFER.LME, then CR0.PG. */
    movl %cr4, %eax
    orl $0x30, %eax
    movl %eax, %cr4

    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    movl $boot_pml4, %eax
    movl %eax, %cr3
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    lgdt boot_gdt_ptr
    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    leaq boot_stack_top(%rip), %rsp
    andq $-16, %rsp
    xorq %rbp, %rbp
    call kernel_main
2:
    cli
    hlt
    jmp 2b

.section .rodata, "a"
.align 8
boot_gdt:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF      /* 64-bit code */
    .quad 0x00CF92000000FFFF      /* data */
boot_gdt_end:
boot_gdt_ptr:
    .word boot_gdt_end - boot_gdt - 1
    .long boot_gdt

.global boot_pml4, boot_pdpt, boot_pd0
.section .bss.boot_tables, "aw", @nobits
.align 4096
boot_pml4: .skip 4096
.align 4096
boot_pdpt: .skip 4096
.align 4096
boot_pd0:  .skip 4096
.align 4096
boot_pd1:  .skip 4096
.align 4096
boot_pd2:  .skip 4096
.align 4096
boot_pd3:  .skip 4096
.align 16
boot_stack: .skip 32768
boot_stack_top:
