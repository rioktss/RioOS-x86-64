#include "mmu.h"
#include "uart.h"
#include "memory.h"

/* x86-64 ring-3 isolation. The bootstrap identity map uses 2 MiB supervisor
   pages. For the reserved user window [MMU_USER_BASE, MMU_USER_STACK_TOP) the
   four covering page-directory entries are swapped for 4 KiB page tables while
   a user program runs, and restored to supervisor 2 MiB pages afterwards. */
extern "C" uint64_t boot_pml4[512];
extern "C" uint64_t boot_pdpt[512];
extern "C" uint64_t boot_pd0[512];

#define PD_FIRST   (MMU_USER_BASE / 0x200000ULL)
#define PD_COUNT   ((MMU_USER_STACK_TOP - MMU_USER_BASE) / 0x200000ULL)
#define USER_PAGES ((MMU_USER_STACK_TOP - MMU_USER_BASE) / MMU_PAGE_SIZE)
#define STACK_FIRST_PAGE ((MMU_USER_STACK_BASE - MMU_USER_BASE) / MMU_PAGE_SIZE)

static uint64_t user_pt[PD_COUNT][512] __attribute__((aligned(4096)));
static uint8_t page_mode[USER_PAGES];
static int user_active = 0;

static void flush_tlb()
{
    uint64_t cr3;
    asm volatile("mov %%cr3, %0\n mov %0, %%cr3" : "=r"(cr3) :: "memory");
}

void mmu_init()
{
    /* Upper-level entries must carry U so that user PTEs can grant access. */
    boot_pml4[0] |= 4ULL;
    boot_pdpt[0] |= 4ULL;
    /* EFER.NXE so non-executable user pages can be expressed. */
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080U));
    lo |= 0x800U;
    asm volatile("wrmsr" :: "a"(lo), "d"(hi), "c"(0xC0000080U));
    flush_tlb();
    uart_puts("x86-64 paging initialized (identity map, user window prepared for ring 3).\r\n");
}
int mmu_enabled() { return 1; }
uint64_t mmu_read_sctlr() { uint64_t v; asm volatile("mov %%cr0, %0" : "=r"(v)); return v; }
uint64_t mmu_read_ttbr0() { uint64_t v; asm volatile("mov %%cr3, %0" : "=r"(v)); return v; }
uint64_t mmu_read_tcr() { uint64_t v; asm volatile("mov %%cr4, %0" : "=r"(v)); return v; }
void mmu_status()
{
    uart_puts("Architecture: x86-64; identity map of first 4 GiB; user window 0x07000000-0x07800000 isolated per program (ring 3).\r\n");
}

int mmu_user_prepare()
{
    user_active = 0;
    for (uint64_t i = 0; i < PD_COUNT; ++i)
        boot_pd0[PD_FIRST + i] = ((PD_FIRST + i) * 0x200000ULL) | 0x083ULL;
    for (uint64_t i = 0; i < USER_PAGES; ++i) page_mode[i] = 0;
    for (uint64_t t = 0; t < PD_COUNT; ++t)
        for (uint64_t e = 0; e < 512; ++e) user_pt[t][e] = 0;
    flush_tlb();
    return 0;
}

int mmu_user_set_range(uint64_t start, uint64_t end, uint32_t flags)
{
    if (end <= start || (start & (MMU_PAGE_SIZE - 1ULL)) != 0) return -1;
    if (start < MMU_USER_BASE || end > MMU_USER_LIMIT) return -1;
    if (flags == 0 || (flags & ~7U) != 0 || ((flags & MMU_USER_WRITE) && (flags & MMU_USER_EXEC))) return -1;
    uint64_t last = (end + MMU_PAGE_SIZE - 1ULL) & ~(MMU_PAGE_SIZE - 1ULL);
    for (uint64_t va = start; va < last; va += MMU_PAGE_SIZE)
        page_mode[(va - MMU_USER_BASE) / MMU_PAGE_SIZE] = (uint8_t)flags;
    return 0;
}

int mmu_user_finish()
{
    /* Private, zeroed, RW stack. Kernel still has the 2 MiB identity mapping here. */
    for (uint64_t va = MMU_USER_STACK_BASE; va < MMU_USER_STACK_TOP; va += 8)
        *(volatile uint64_t*)va = 0;
    for (uint64_t i = STACK_FIRST_PAGE; i < USER_PAGES; ++i)
        page_mode[i] = (uint8_t)(MMU_USER_READ | MMU_USER_WRITE);
    for (uint64_t i = 0; i < USER_PAGES; ++i)
    {
        uint8_t m = page_mode[i];
        uint64_t pte = 0;
        if (m)
        {
            pte = (MMU_USER_BASE + i * MMU_PAGE_SIZE) | 1ULL | 4ULL;
            if (m & MMU_USER_WRITE) pte |= 2ULL;
            if (!(m & MMU_USER_EXEC)) pte |= (1ULL << 63);
        }
        user_pt[i / 512][i % 512] = pte;
    }
    for (uint64_t t = 0; t < PD_COUNT; ++t)
        boot_pd0[PD_FIRST + t] = (uint64_t)&user_pt[t][0] | 0x7ULL;
    flush_tlb();
    user_active = 1;
    return 0;
}

int mmu_user_pointer_ok(uint64_t address, uint64_t length, int write)
{
    if (!user_active || address == 0 || length == 0) return 0;
    if (address < MMU_USER_BASE || address >= MMU_USER_STACK_TOP) return 0;
    if (length > MMU_USER_STACK_TOP - address) return 0;
    uint64_t end = address + length;
    for (uint64_t pos = address & ~(MMU_PAGE_SIZE - 1ULL); pos < end; pos += MMU_PAGE_SIZE)
    {
        uint8_t m = page_mode[(pos - MMU_USER_BASE) / MMU_PAGE_SIZE];
        if (!(m & MMU_USER_READ)) return 0;
        if (write && !(m & MMU_USER_WRITE)) return 0;
    }
    return 1;
}
uint64_t mmu_user_stack_top() { return MMU_USER_STACK_TOP; }
