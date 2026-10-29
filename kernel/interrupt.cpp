#include "interrupt.h"
#include "uart.h"
#include "timer.h"

static volatile uint64_t irq_total = 0;
static volatile uint64_t irq_timer = 0;
static volatile uint64_t irq_spurious = 0;
static volatile uint64_t irq_unhandled = 0;
static volatile uint64_t irq_nested = 0;
static volatile uint32_t irq_depth = 0;
static volatile uint32_t irq_max_depth = 0;
static volatile uint32_t last_irq = 0xFFFFFFFFU;

static inline void outb(uint16_t port, uint8_t value)
{ asm volatile("outb %0, %1" :: "a"(value), "Nd"(port)); }
static inline uint8_t inb(uint16_t port)
{ uint8_t value; asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static inline void io_wait() { outb(0x80, 0); }
static inline uint64_t read_flags()
{ uint64_t f; asm volatile("pushfq; popq %0" : "=r"(f)); return f; }
extern "C" void x86_irq0_stub();
extern "C" void* x86_irq_stub_table[15];
extern "C" void x86_idt_set_gate(uint32_t vector, uint64_t target);

void interrupt_init()
{
    asm volatile("cli" ::: "memory");
    /* Remap the dual 8259 PIC to vectors 0x20..0x2F, then mask all lines. */
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    x86_idt_set_gate(32U, (uint64_t)x86_irq0_stub);
    for (uint32_t i = 0; i < 15U; ++i) x86_idt_set_gate(33U + i, (uint64_t)x86_irq_stub_table[i]);
    irq_total = irq_timer = irq_spurious = irq_unhandled = irq_nested = 0;
    irq_depth = irq_max_depth = 0;
    last_irq = 0xFFFFFFFFU;
    uart_puts("Interrupt controller initialized (8259 PIC remapped; IRQs masked by default).\r\n");
}

void interrupt_enable() { asm volatile("sti" ::: "memory"); }
void interrupt_disable() { asm volatile("cli" ::: "memory"); }
uint64_t interrupt_count() { return irq_total; }
uint32_t interrupt_last_irq() { return last_irq; }
void interrupt_get_stats(InterruptStats* out)
{
    if (!out) return;
    out->total = irq_total; out->timer = irq_timer; out->spurious = irq_spurious;
    out->unhandled = irq_unhandled; out->nested = irq_nested; out->last_irq = last_irq;
    out->max_depth = irq_max_depth;
}
int interrupt_cpu_irq_enabled() { return (read_flags() & (1ULL << 9)) != 0ULL; }
void interrupt_idle()
{
    /* hlt only when the 100 Hz PIT tick can wake the CPU again. */
    if (interrupt_cpu_irq_enabled() && timer_mode() == TIMER_MODE_PERIODIC)
        asm volatile("hlt" ::: "memory");
    else
        asm volatile("pause" ::: "memory");
}
void interrupt_timer_line_enable()
{
    uint8_t mask = inb(0x21);
    outb(0x21, (uint8_t)(mask & (uint8_t)~0x01U));
}
void interrupt_timer_line_disable()
{
    uint8_t mask = inb(0x21);
    outb(0x21, (uint8_t)(mask | 0x01U));
}

extern "C" void x86_irq_dispatch()
{
    if (irq_depth != 0U) ++irq_nested;
    ++irq_depth;
    if (irq_depth > irq_max_depth) irq_max_depth = irq_depth;
    ++irq_total;
    ++irq_timer;
    last_irq = 0U;
    timer_interrupt_handler();
    /* IRQ0 originates on the master PIC. */
    outb(0x20, 0x20);
    --irq_depth;
}

/* Any PIC vector other than the PIT: count it, handle spurious IRQ7/15 correctly. */
extern "C" void x86_irq_generic(uint64_t vector)
{
    ++irq_total; ++irq_unhandled;
    last_irq = (uint32_t)(vector - 32U);
    if (vector == 39U)
    {
        outb(0x20, 0x0B);
        if ((inb(0x20) & 0x80U) == 0U) { ++irq_spurious; return; } /* spurious: no EOI */
    }
    else if (vector == 47U)
    {
        outb(0xA0, 0x0B);
        if ((inb(0xA0) & 0x80U) == 0U) { ++irq_spurious; outb(0x20, 0x20); return; }
    }
    if (vector >= 40U) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

extern "C" void interrupt_dispatch(ExceptionFrame*) { ++irq_unhandled; }
extern "C" void interrupt_exception(uint64_t, uint64_t, uint64_t, uint64_t)
{
    ++irq_unhandled;
    uart_puts("Unexpected x86 exception dispatch.\r\n");
}
