#include "uart.h"
#include "types.h"
#include "exceptions.h"
#include "user.h"
#include "fault.h"

extern "C" void x86_user_init();

struct __attribute__((packed)) IdtGate { uint16_t off_lo; uint16_t selector; uint8_t ist; uint8_t attr; uint16_t off_mid; uint32_t off_hi; uint32_t zero; };
struct __attribute__((packed)) Idtr { uint16_t limit; uint64_t base; };
static IdtGate idt[256] __attribute__((aligned(16)));
extern "C" void* x86_isr_stub_table[];
static void idt_set(int n, uint64_t target)
{
    idt[n].off_lo=(uint16_t)target; idt[n].selector=0x08; idt[n].ist=0; idt[n].attr=0x8E;
    idt[n].off_mid=(uint16_t)(target>>16); idt[n].off_hi=(uint32_t)(target>>32); idt[n].zero=0;
}
extern "C" void x86_exception_dispatch(uint64_t* raw)
{
    static const char* names[32] = {"Divide Error","Debug","NMI","Breakpoint","Overflow","BOUND Range","Invalid Opcode","Device Not Available","Double Fault","Coprocessor Segment Overrun","Invalid TSS","Segment Not Present","Stack-Segment Fault","General Protection","Page Fault","Reserved","x87 Floating-Point","Alignment Check","Machine Check","SIMD Floating-Point","Virtualization","Control Protection","Reserved","Reserved","Reserved","Reserved","Reserved","Reserved","Hypervisor Injection","VMM Communication","Security","Reserved"};
    if (!raw) { uart_puts("Fatal x86 exception (no frame).\r\n"); for (;;) asm volatile("cli; hlt"); }
    uint64_t vector=raw[0], error=raw[1], rip=raw[2], cs=raw[3];
    uart_puts("\r\n*** x86-64 CPU EXCEPTION ***\r\nVector: ");
    if (vector<32) uart_puts(names[vector]); else uart_puts("unknown");
    uart_puts("\r\nError: 0x"); static const char h[]="0123456789ABCDEF";
    for (int i=15;i>=0;--i) uart_putc(h[(error>>(i*4))&15]);
    uart_puts("\r\nRIP: 0x"); for (int i=15;i>=0;--i) uart_putc(h[(rip>>(i*4))&15]);
    if ((cs & 3ULL) == 3ULL)
    {
        uint64_t cr2=0; if (vector==14) asm volatile("mov %%cr2, %0" : "=r"(cr2));
        uart_puts("\r\nCR2: 0x"); for (int i=15;i>=0;--i) uart_putc(h[(cr2>>(i*4))&15]);
        uart_puts("\r\n[ring 3] Fatal user exception; terminating process.\r\n");
        fault_note(1);
        user_set_exit_code(-14);
        return; /* the stub resumes at user_exit_return */
    }
    fault_note(0);
    uart_puts("\r\nHalting CPU.\r\n");
    for (;;) asm volatile("cli; hlt");
}
extern "C" void x86_idt_set_gate(uint32_t vector, uint64_t target)
{
    if (vector < 256U && target != 0) idt_set((int)vector, target);
}

extern "C" void exceptions_init()
{
    for (int i=0;i<256;++i) { idt[i].off_lo=0; idt[i].selector=0x08; idt[i].ist=0; idt[i].attr=0; idt[i].off_mid=0; idt[i].off_hi=0; idt[i].zero=0; }
    for (int i=0;i<32;++i) idt_set(i,(uint64_t)x86_isr_stub_table[i]);
    Idtr idtr={(uint16_t)(sizeof(idt)-1),(uint64_t)idt};
    asm volatile("lidt %0" :: "m"(idtr) : "memory");
    x86_user_init();
    uart_puts("x86-64 IDT initialized (CPU exceptions 0-31).\r\n");
}
extern "C" void exception_sync_el1(void*) {}
extern "C" void exception_sync_el0(void*) {}
extern "C" void exception_irq_unhandled() {}
