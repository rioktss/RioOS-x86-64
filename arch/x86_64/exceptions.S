.section .text, "ax"
.code64
.extern x86_exception_dispatch
.extern user_exit_return
.global x86_isr_stub_table
.macro ISR_NOERR n
.global x86_isr\n
x86_isr\n:
    cli
    pushq $0
    pushq $\n
    jmp x86_exception_common
.endm
.macro ISR_ERR n
.global x86_isr\n
x86_isr\n:
    cli
    pushq $\n
    jmp x86_exception_common
.endm
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR 29
ISR_ERR 30
ISR_NOERR 31
x86_exception_common:
    cld
    movq %rsp, %rdi
    andq $-16, %rsp
    call x86_exception_dispatch
    jmp user_exit_return

.section .rodata, "a"
.align 8
x86_isr_stub_table:
    .quad x86_isr0, x86_isr1, x86_isr2, x86_isr3
    .quad x86_isr4, x86_isr5, x86_isr6, x86_isr7
    .quad x86_isr8, x86_isr9, x86_isr10, x86_isr11
    .quad x86_isr12, x86_isr13, x86_isr14, x86_isr15
    .quad x86_isr16, x86_isr17, x86_isr18, x86_isr19
    .quad x86_isr20, x86_isr21, x86_isr22, x86_isr23
    .quad x86_isr24, x86_isr25, x86_isr26, x86_isr27
    .quad x86_isr28, x86_isr29, x86_isr30, x86_isr31

/* Remapped legacy PIC IRQ0 (PIT). Preserve every GPR and return with IRETQ. */
.global x86_irq0_stub
.type x86_irq0_stub, @function
.extern x86_irq_dispatch
x86_irq0_stub:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %r12
    andq $-16, %rsp
    call x86_irq_dispatch
    movq %r12, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    iretq

/* Remaining legacy PIC vectors 0x21..0x2F: all lines are masked by default, but
   spurious IRQ7/IRQ15 and any unmasked line must not hit a missing IDT gate. */
.extern x86_irq_generic
.macro IRQ_STUB n
x86_irq\n\()_stub:
    pushq $\n
    jmp x86_irq_common
.endm
IRQ_STUB 33
IRQ_STUB 34
IRQ_STUB 35
IRQ_STUB 36
IRQ_STUB 37
IRQ_STUB 38
IRQ_STUB 39
IRQ_STUB 40
IRQ_STUB 41
IRQ_STUB 42
IRQ_STUB 43
IRQ_STUB 44
IRQ_STUB 45
IRQ_STUB 46
IRQ_STUB 47
x86_irq_common:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq 120(%rsp), %rdi
    movq %rsp, %r12
    andq $-16, %rsp
    cld
    call x86_irq_generic
    movq %r12, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    addq $8, %rsp
    iretq

.section .rodata, "a"
.align 8
.global x86_irq_stub_table
x86_irq_stub_table:
    .quad x86_irq33_stub, x86_irq34_stub, x86_irq35_stub, x86_irq36_stub
    .quad x86_irq37_stub, x86_irq38_stub, x86_irq39_stub, x86_irq40_stub
    .quad x86_irq41_stub, x86_irq42_stub, x86_irq43_stub, x86_irq44_stub
    .quad x86_irq45_stub, x86_irq46_stub, x86_irq47_stub
