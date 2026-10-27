#include "types.h"
#include "uart.h"

/* Final GDT: 0x08/0x10 kernel code/data, 0x18 compat code (STAR base),
   0x20 user data, 0x28 user 64-bit code, 0x30 TSS (16 bytes). */
struct __attribute__((packed)) Tss64 {
    uint32_t reserved0; uint64_t rsp0; uint64_t rsp1; uint64_t rsp2; uint64_t reserved1;
    uint64_t ist[7]; uint64_t reserved2; uint16_t reserved3; uint16_t iomap;
};
struct __attribute__((packed)) Gdtr { uint16_t limit; uint64_t base; };

extern "C" { Tss64 x86_tss; }
static uint64_t gdt[8] __attribute__((aligned(16)));
extern "C" void x86_syscall_entry();

extern "C" void x86_user_init()
{
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFULL;
    gdt[2] = 0x00CF92000000FFFFULL;
    gdt[3] = 0x00CFFA000000FFFFULL;
    gdt[4] = 0x00CFF2000000FFFFULL;
    gdt[5] = 0x00AFFA000000FFFFULL;
    uint64_t base = (uint64_t)&x86_tss;
    uint64_t limit = sizeof(Tss64) - 1;
    gdt[6] = (limit & 0xFFFFULL) | ((base & 0xFFFFFFULL) << 16) | (0x89ULL << 40) |
             (((limit >> 16) & 0xFULL) << 48) | (((base >> 24) & 0xFFULL) << 56);
    gdt[7] = base >> 32;
    x86_tss.iomap = sizeof(Tss64);
    Gdtr g = { (uint16_t)(sizeof(gdt) - 1), (uint64_t)gdt };
    asm volatile("lgdt %0" :: "m"(g) : "memory");
    uint16_t tr = 0x30;
    asm volatile("ltr %0" :: "r"(tr));
    /* EFER.SCE, STAR, LSTAR, SFMASK (clear IF, DF, TF, IOPL, NT, AC on entry). */
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080U));
    lo |= 1U;
    asm volatile("wrmsr" :: "a"(lo), "d"(hi), "c"(0xC0000080U));
    uint64_t star = (0x0018ULL << 48) | (0x0008ULL << 32);
    asm volatile("wrmsr" :: "a"((uint32_t)star), "d"((uint32_t)(star >> 32)), "c"(0xC0000081U));
    uint64_t lstar = (uint64_t)x86_syscall_entry;
    asm volatile("wrmsr" :: "a"((uint32_t)lstar), "d"((uint32_t)(lstar >> 32)), "c"(0xC0000082U));
    uint64_t mask = 0x47700ULL;
    asm volatile("wrmsr" :: "a"((uint32_t)mask), "d"(0U), "c"(0xC0000084U));
    uart_puts("x86-64 GDT/TSS and SYSCALL entry initialized (ring 3 ready).\r\n");
}
