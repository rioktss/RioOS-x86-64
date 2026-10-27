/* RioOS x86-64 userspace self-test: write, uptime, getpid, exit(0). */
.section .text
.global _start
_start:
    movl $1, %eax
    leaq msg(%rip), %rdi
    syscall
    movl $2, %eax
    syscall
    movl $3, %eax
    syscall
    movl $6, %eax
    xorl %edi, %edi
    syscall
1:  pause
    jmp 1b

.section .rodata
msg:
    .asciz "[ring3] MyKernel userspace is alive.\r\n"
