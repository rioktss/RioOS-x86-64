.section .text, "ax"
.code64
.global thread_context_switch
.global thread_trampoline
.extern thread_entry_hook
.extern thread_exit
/* SysV x86-64: rdi = old SP slot, rsi = new saved stack pointer. */
thread_context_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
/* Initial synthetic context keeps entry in r12 and argument in r13. */
thread_trampoline:
    /* Entry RSP is 8 mod 16; realign before ABI-conforming C calls. */
    subq $8, %rsp
    call thread_entry_hook
    movq %r13, %rdi
    call *%r12
    call thread_exit
1:  cli
    hlt
    jmp 1b
