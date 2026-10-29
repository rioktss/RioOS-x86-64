.section .text, "ax"
.code64
.global user_enter
.global user_exit_return
.global x86_syscall_entry
.extern x86_syscall_c
.extern x86_tss

/* user_enter(entry = rdi, stack_top = rsi). Synchronous: returns to the caller
   when the program exits or faults (via user_exit_return). Callee-saved
   registers are parked on the kernel stack, whose pointer is also TSS.RSP0
   (exceptions) and the syscall entry stack. */
user_enter:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, x86_user_kernel_rsp(%rip)
    movq %rsp, x86_tss+4(%rip)
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    pushq $0x23          /* SS */
    leaq -8(%rsi), %rsi  /* entry looks like a call: RSP = 8 mod 16 */
    pushq %rsi           /* RSP */
    pushq $0x202         /* RFLAGS: IF=1, the PIT tick keeps running in ring 3 */
    pushq $0x2B          /* CS: 64-bit ring-3 code */
    pushq %rdi           /* RIP */
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %r10d, %r10d
    xorl %r11d, %r11d
    xorl %r12d, %r12d
    xorl %r13d, %r13d
    xorl %r14d, %r14d
    xorl %r15d, %r15d
    iretq

/* Reached by jmp from the exit syscall or from a fatal user fault. */
user_exit_return:
    cli
    cld
    movq x86_user_kernel_rsp(%rip), %rsp
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

/* SYSCALL entry: rax = number, rdi,rsi,rdx,r10,r8,r9 = args, rcx = user RIP,
   r11 = user RFLAGS. Builds an ExceptionFrame (x[0..5] args, x[8] number). */
x86_syscall_entry:
    movq %rsp, x86_user_rsp(%rip)
    movq x86_user_kernel_rsp(%rip), %rsp
    pushq x86_user_rsp(%rip)
    pushq %r11
    pushq %rcx
    pushq %rax
    pushq %rbx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rbx          /* save area; callee-saved across the C call */
    subq $304, %rsp
    andq $-16, %rsp
    movq %rsp, %rdi
    xorl %eax, %eax
    movl $38, %ecx
    cld
    rep stosq
    movq 72(%rbx), %rax
    movq %rax, 0(%rsp)       /* x0 = rdi */
    movq 64(%rbx), %rax
    movq %rax, 8(%rsp)       /* x1 = rsi */
    movq 56(%rbx), %rax
    movq %rax, 16(%rsp)      /* x2 = rdx */
    movq 32(%rbx), %rax
    movq %rax, 24(%rsp)      /* x3 = r10 */
    movq 48(%rbx), %rax
    movq %rax, 32(%rsp)      /* x4 = r8 */
    movq 40(%rbx), %rax
    movq %rax, 40(%rsp)      /* x5 = r9 */
    movq 96(%rbx), %rax
    movq %rax, 64(%rsp)      /* x8 = syscall number */
    movq 104(%rbx), %rax
    movq %rax, 248(%rsp)     /* elr */
    movq 112(%rbx), %rax
    movq %rax, 256(%rsp)     /* spsr */
    movq %rsp, %rdi
    movl $1, %esi
    call x86_syscall_c
    cmpq $0x3C5, 256(%rsp)
    je user_exit_return
    movq 0(%rsp), %rax
    movq %rax, 96(%rbx)      /* result -> saved rax */
    movq %rbx, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r10
    popq %r9
    popq %r8
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rbx
    popq %rax
    popq %rcx
    popq %r11
    popq %rsp
    sysretq

.section .data
.align 8
.global x86_user_kernel_rsp
x86_user_kernel_rsp: .quad 0
x86_user_rsp: .quad 0
