#include "timer.h"
#include "scheduler.h"
#include "uart.h"

#define PIT_INPUT_HZ 1193182U
#define PIT_COMMAND  0x43
#define PIT_CHANNEL0 0x40

static uint64_t start_tsc = 0;
static uint64_t tsc_hz = 1000000000ULL;
static volatile uint32_t mode_value = TIMER_MODE_OFF;
static volatile uint32_t current_hz = 0;
static volatile uint64_t events = 0;
static volatile uint64_t stray = 0;
static volatile uint64_t oneshot = 0;
static volatile uint32_t oneshot_left = 0;
static int sched_tick = 1;
static inline uint64_t rdtsc()
{
    uint32_t lo, hi; asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static inline void cpuid(uint32_t leaf, uint32_t sub, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d)
{ asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub)); }
static inline void outb(uint16_t port, uint8_t value)
{ asm volatile("outb %0, %1" :: "a"(value), "Nd"(port)); }

static inline uint8_t inb(uint16_t port)
{ uint8_t value; asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }

/* Measure the TSC against PIT channel 2 (10 ms one-shot, gate via port 0x61).
   Returns 0 when the measurement is implausible. */
static uint64_t calibrate_tsc_with_pit()
{
    const uint32_t count = 11932U; /* 10.0 ms at 1.193182 MHz */
    uint8_t p61 = inb(0x61);
    outb(0x61, (uint8_t)(p61 & ~0x03U));        /* gate off, speaker off */
    outb(PIT_COMMAND, 0xB0U);                    /* ch2, lo/hi, mode 0, binary */
    outb(0x42, (uint8_t)(count & 0xFFU));
    outb(0x42, (uint8_t)(count >> 8));
    outb(0x61, (uint8_t)((p61 & ~0x02U) | 0x01U)); /* gate on: counting starts */
    uint64_t t0 = rdtsc();
    uint32_t guard = 0;
    while ((inb(0x61) & 0x20U) == 0U)
        if (++guard > 20000000U) { outb(0x61, p61); return 0; }
    uint64_t t1 = rdtsc();
    outb(0x61, p61);
    uint64_t hz = (t1 - t0) * 1193182ULL / count;
    if (hz < 200000000ULL || hz > 10000000000ULL) return 0;
    return hz;
}

static void pit_set_hz(uint32_t hz)
{
    if (hz < 19U) hz = 19U;
    if (hz > 1000U) hz = 1000U;
    uint32_t divisor = PIT_INPUT_HZ / hz;
    if (divisor < 1U) divisor = 1U;
    if (divisor > 65535U) divisor = 65535U;
    outb(PIT_COMMAND, 0x36U); /* channel 0, lobyte/hibyte, mode 3, binary */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFFU));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));
}

void timer_init()
{
    uint32_t max_leaf=0,b=0,c=0,d=0;
    cpuid(0,0,&max_leaf,&b,&c,&d);
    uint64_t detected_hz=0;
    if (max_leaf >= 0x15U) {
        uint32_t den=0,num=0,crystal=0,unused=0;
        cpuid(0x15U,0,&den,&num,&crystal,&unused);
        if (den && num && crystal) detected_hz=(uint64_t)crystal*num/den;
    }
    if (!detected_hz && max_leaf >= 0x16U) {
        uint32_t mhz=0,unused_b=0,unused_c=0,unused_d=0;
        cpuid(0x16U,0,&mhz,&unused_b,&unused_c,&unused_d);
        if (mhz) detected_hz=(uint64_t)mhz*1000000ULL;
    }
    if (!detected_hz) detected_hz = calibrate_tsc_with_pit();
    if (detected_hz) tsc_hz=detected_hz;
    start_tsc = rdtsc(); mode_value=TIMER_MODE_OFF; current_hz=0;
    oneshot_left=0; events=0; stray=0; oneshot=0;
    uart_puts("TSC clock initialized; PIT 8254 is the 100 Hz system tick source.\r\n");
}
void timer_start_periodic(uint32_t hz)
{
    if (hz < 19U) hz = 19U;
    if (hz > 1000U) hz = 1000U;
    pit_set_hz(hz);
    current_hz = hz;
    oneshot_left = 0;
    mode_value = TIMER_MODE_PERIODIC;
}
void timer_start_oneshot_ms(uint32_t ms)
{
    if (ms == 0U) ms = 1U;
    /* A 1 kHz PIT tick provides a millisecond-granularity one-shot. */
    pit_set_hz(1000U);
    current_hz = 1000U;
    oneshot_left = ms;
    mode_value = TIMER_MODE_ONESHOT;
}
void timer_stop() { mode_value=TIMER_MODE_OFF; current_hz=0; oneshot_left=0; }
void timer_interrupt_handler()
{
    if (mode_value == TIMER_MODE_OFF) { ++stray; return; }
    ++events;
    if (mode_value == TIMER_MODE_ONESHOT)
    {
        if (oneshot_left > 0U) --oneshot_left;
        if (oneshot_left == 0U)
        {
            ++oneshot;
            mode_value = TIMER_MODE_OFF;
            current_hz = 0;
        }
    }
    /* Stage D: the periodic tick drives the scheduler tick counter. Context
       switches stay cooperative (no IRQ preemption). */
    if (mode_value == TIMER_MODE_PERIODIC && sched_tick) (void)scheduler_tick();
}
void timer_set_sched_tick(int enabled) { sched_tick=enabled?1:0; }
uint32_t timer_mode() { return mode_value; }
uint32_t timer_current_hz() { return current_hz; }
uint64_t timer_irq_count() { return events; }
uint64_t timer_stray_count() { return stray; }
uint64_t timer_oneshot_fired() { return oneshot; }
uint64_t timer_ticks() { return rdtsc()-start_tsc; }
uint64_t timer_frequency() { return tsc_hz; }
uint64_t timer_millis() { if (!tsc_hz) return 0; return (timer_ticks()*1000ULL)/tsc_hz; }
uint64_t timer_seconds() { return timer_millis()/1000ULL; }
void timer_delay_ms(uint64_t ms) { uint64_t end=timer_millis()+ms; while (timer_millis()<end) asm volatile("pause"); }
