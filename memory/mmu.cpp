#include "mmu.h"
#include "uart.h"
#include "page_alloc.h"

/* x86-64 ring-3 isolation (4-level paging, 4 KiB user pages).

   Boot identity-maps the first 4 GiB with 2 MiB supervisor pages. The user
   window [MMU_USER_BASE, MMU_USER_STACK_TOP) is covered by four page-directory
   entries. Each process (P5) owns a private PML4/PDPT/PD plus four 4 KiB page
   tables and private physical frames; everything outside the window is shared
   with the kernel's identity map. The legacy single-image path swaps the four
   boot PD entries in place and restores them in mmu_user_prepare(). */
extern "C" uint64_t boot_pml4[512];
extern "C" uint64_t boot_pdpt[512];
extern "C" uint64_t boot_pd0[512];

#define PD_FIRST         (MMU_USER_BASE / 0x200000ULL)
#define PD_COUNT         ((MMU_USER_STACK_TOP - MMU_USER_BASE) / 0x200000ULL)
#define USER_PAGES       ((MMU_USER_STACK_TOP - MMU_USER_BASE) / MMU_PAGE_SIZE)
#define MAX_USER_PAGES   ((MMU_USER_LIMIT - MMU_USER_BASE) / MMU_PAGE_SIZE)
#define USER_STACK_PAGES ((MMU_USER_STACK_TOP - MMU_USER_STACK_BASE) / MMU_PAGE_SIZE)
#define STACK_FIRST_PAGE ((MMU_USER_STACK_BASE - MMU_USER_BASE) / MMU_PAGE_SIZE)
#define USER_AS_MAX      32

#define PTE_P  1ULL
#define PTE_W  2ULL
#define PTE_U  4ULL
#define PTE_PS 0x80ULL
#define PTE_NX (1ULL << 63)

#define USER_R 1U
#define USER_W 2U
#define USER_X 4U

/* Legacy single-image tables (used while no per-process space is active). */
static uint64_t user_pt[PD_COUNT][512] __attribute__((aligned(4096)));
static uint8_t page_mode[USER_PAGES];
static int user_active = 0;

struct UserAddressSpace
{
    int used;
    int pid;
    int ready;
};
static UserAddressSpace user_spaces[USER_AS_MAX];
static uint64_t as_pml4[USER_AS_MAX][512] __attribute__((aligned(4096)));
static uint64_t as_pdpt[USER_AS_MAX][512] __attribute__((aligned(4096)));
static uint64_t as_pd[USER_AS_MAX][512] __attribute__((aligned(4096)));
static uint64_t as_pt[USER_AS_MAX][PD_COUNT][512] __attribute__((aligned(4096)));
static uint64_t user_frames[USER_AS_MAX][MAX_USER_PAGES];
static uint64_t stack_frames[USER_AS_MAX][USER_STACK_PAGES];
static uint8_t user_space_modes[USER_AS_MAX][MAX_USER_PAGES];
static uint8_t user_stack_modes[USER_AS_MAX][USER_STACK_PAGES];
static int active_user_space = -1;

static void flush_tlb()
{
    uint64_t cr3;
    asm volatile("mov %%cr3, %0\n mov %0, %%cr3" : "=r"(cr3) :: "memory");
}

static void load_cr3(uint64_t root)
{
    asm volatile("mov %0, %%cr3" :: "r"(root) : "memory");
}

static int user_space_find(int pid)
{
    for (int i = 0; i < USER_AS_MAX; ++i)
        if (user_spaces[i].used && user_spaces[i].pid == pid) return i;
    return -1;
}

static uint64_t user_pte(uint64_t frame, uint32_t mode)
{
    uint64_t pte = frame | PTE_P | PTE_U;
    if (mode & USER_W) pte |= PTE_W;
    if (!(mode & USER_X)) pte |= PTE_NX;
    return pte;
}

void mmu_init()
{
    /* Upper-level entries must carry U so that user PTEs can grant access. */
    boot_pml4[0] |= PTE_U;
    boot_pdpt[0] |= PTE_U;
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

int mmu_user_prepare()
{
    /* Return to the boot address space before touching its tables. */
    if (active_user_space >= 0) (void)mmu_user_space_deactivate();
    user_active = 0;
    for (uint64_t i = 0; i < PD_COUNT; ++i)
        boot_pd0[PD_FIRST + i] = ((PD_FIRST + i) * 0x200000ULL) | PTE_P | PTE_W | PTE_PS;
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
        page_mode[i] = (uint8_t)(USER_R | USER_W);
    for (uint64_t i = 0; i < USER_PAGES; ++i)
    {
        uint8_t m = page_mode[i];
        user_pt[i / 512][i % 512] = m ? user_pte(MMU_USER_BASE + i * MMU_PAGE_SIZE, m) : 0;
    }
    for (uint64_t t = 0; t < PD_COUNT; ++t)
        boot_pd0[PD_FIRST + t] = (uint64_t)&user_pt[t][0] | (PTE_P | PTE_W | PTE_U);
    flush_tlb();
    user_active = 1;
    return 0;
}

int mmu_user_pointer_ok(uint64_t address, uint64_t length, int write)
{
    if (address == 0 || length == 0) return 0;
    if (address < MMU_USER_BASE || address >= MMU_USER_STACK_TOP) return 0;
    if (length > MMU_USER_STACK_TOP - address) return 0;
    uint64_t end = address + length;

    if (active_user_space >= 0)
    {
        int slot = active_user_space;
        uint64_t pos = address & ~(MMU_PAGE_SIZE - 1ULL);
        while (pos < end)
        {
            int okay = 0;
            if (pos >= MMU_USER_BASE && pos < MMU_USER_LIMIT)
            {
                uint64_t idx = (pos - MMU_USER_BASE) / MMU_PAGE_SIZE;
                uint8_t mode = user_space_modes[slot][idx];
                okay = user_frames[slot][idx] != 0 && (mode & USER_R) != 0 &&
                       (!write || (mode & USER_W) != 0);
            }
            else if (pos >= MMU_USER_STACK_BASE && pos < MMU_USER_STACK_TOP)
            {
                uint64_t idx = (pos - MMU_USER_STACK_BASE) / MMU_PAGE_SIZE;
                okay = idx < USER_STACK_PAGES && stack_frames[slot][idx] != 0 &&
                       (!write || (user_stack_modes[slot][idx] & USER_W) != 0);
            }
            if (!okay) return 0;
            pos += MMU_PAGE_SIZE;
        }
        return 1;
    }

    if (!user_active) return 0;
    for (uint64_t pos = address & ~(MMU_PAGE_SIZE - 1ULL); pos < end; pos += MMU_PAGE_SIZE)
    {
        uint8_t m = page_mode[(pos - MMU_USER_BASE) / MMU_PAGE_SIZE];
        if (!(m & USER_R)) return 0;
        if (write && !(m & USER_W)) return 0;
    }
    return 1;
}

uint64_t mmu_user_stack_top() { return MMU_USER_STACK_TOP; }

int mmu_user_space_exists(int pid) { return user_space_find(pid) >= 0; }

int mmu_user_space_active_pid()
{
    return active_user_space >= 0 ? user_spaces[active_user_space].pid : -1;
}

int mmu_user_space_create(int pid)
{
    if (pid < 0 || !page_alloc_ready()) return -1;
    if (user_space_find(pid) >= 0) return -2;

    int slot = -1;
    for (int i = 0; i < USER_AS_MAX; ++i)
        if (!user_spaces[i].used) { slot = i; break; }
    if (slot < 0) return -3;

    user_spaces[slot].used = 1;
    user_spaces[slot].pid = pid;
    user_spaces[slot].ready = 0;
    for (uint64_t i = 0; i < MAX_USER_PAGES; ++i) { user_frames[slot][i] = 0; user_space_modes[slot][i] = 0; }
    for (uint64_t i = 0; i < USER_STACK_PAGES; ++i) { stack_frames[slot][i] = 0; user_stack_modes[slot][i] = 0; }

    /* Private PML4 -> PDPT -> PD chain; all other entries mirror the boot map. */
    for (int i = 0; i < 512; ++i)
    {
        as_pml4[slot][i] = boot_pml4[i];
        as_pdpt[slot][i] = boot_pdpt[i];
        as_pd[slot][i] = boot_pd0[i];
    }
    as_pml4[slot][0] = (uint64_t)as_pdpt[slot] | (PTE_P | PTE_W | PTE_U);
    as_pdpt[slot][0] = (uint64_t)as_pd[slot] | (PTE_P | PTE_W | PTE_U);
    for (uint64_t t = 0; t < PD_COUNT; ++t)
    {
        for (int e = 0; e < 512; ++e) as_pt[slot][t][e] = 0;
        as_pd[slot][PD_FIRST + t] = (uint64_t)as_pt[slot][t] | (PTE_P | PTE_W | PTE_U);
    }

    /* Only the top 64 KiB is mapped as stack, each page with its own frame;
       the rest of the window below it is a guard (not present). */
    for (uint64_t i = 0; i < USER_STACK_PAGES; ++i)
    {
        void* frame = allocate_page();
        if (frame == 0)
        {
            (void)mmu_user_space_destroy(pid);
            return -4;
        }
        stack_frames[slot][i] = (uint64_t)frame;
        user_stack_modes[slot][i] = (uint8_t)(USER_R | USER_W);
        uint64_t page = STACK_FIRST_PAGE + i;
        as_pt[slot][page / 512][page % 512] = user_pte((uint64_t)frame, USER_R | USER_W);
    }
    return 0;
}

int mmu_user_space_map(int pid, uint64_t start, uint64_t end, uint32_t flags)
{
    int slot = user_space_find(pid);
    if (slot < 0 || user_spaces[slot].ready || end <= start ||
        start < MMU_USER_BASE || end > MMU_USER_LIMIT ||
        (start & (MMU_PAGE_SIZE - 1ULL)) != 0 || flags == 0 ||
        (flags & ~(USER_R | USER_W | USER_X)) != 0 ||
        ((flags & USER_W) != 0 && (flags & USER_X) != 0))
        return -1;

    uint64_t rounded_end = (end + MMU_PAGE_SIZE - 1ULL) & ~(MMU_PAGE_SIZE - 1ULL);
    if (rounded_end < end || rounded_end > MMU_USER_LIMIT) return -1;
    uint64_t first = (start - MMU_USER_BASE) / MMU_PAGE_SIZE;
    uint64_t last = (rounded_end - MMU_USER_BASE) / MMU_PAGE_SIZE;
    if (last <= first || last > MAX_USER_PAGES) return -1;

    for (uint64_t i = first; i < last; ++i)
        if (user_frames[slot][i] != 0 || user_space_modes[slot][i] != 0) return -2;

    uint64_t i = first;
    for (; i < last; ++i)
    {
        void* frame = allocate_page();
        if (frame == 0) break;
        user_frames[slot][i] = (uint64_t)frame;
        user_space_modes[slot][i] = (uint8_t)flags;
        as_pt[slot][i / 512][i % 512] = user_pte((uint64_t)frame, flags);
    }
    if (i != last)
    {
        for (uint64_t j = first; j < i; ++j)
        {
            (void)free_page((void*)user_frames[slot][j]);
            user_frames[slot][j] = 0;
            user_space_modes[slot][j] = 0;
            as_pt[slot][j / 512][j % 512] = 0;
        }
        return -4;
    }
    return 0;
}

void* mmu_user_space_pointer(int pid, uint64_t address)
{
    int slot = user_space_find(pid);
    if (slot < 0) return 0;
    if (address >= MMU_USER_BASE && address < MMU_USER_LIMIT)
    {
        uint64_t index = (address - MMU_USER_BASE) / MMU_PAGE_SIZE;
        if (user_frames[slot][index] == 0) return 0;
        return (void*)(user_frames[slot][index] + (address & (MMU_PAGE_SIZE - 1ULL)));
    }
    if (address >= MMU_USER_STACK_BASE && address < MMU_USER_STACK_TOP)
    {
        uint64_t index = (address - MMU_USER_STACK_BASE) / MMU_PAGE_SIZE;
        if (index >= USER_STACK_PAGES || stack_frames[slot][index] == 0) return 0;
        return (void*)(stack_frames[slot][index] + (address & (MMU_PAGE_SIZE - 1ULL)));
    }
    return 0;
}

int mmu_user_space_copy_to(int pid, uint64_t destination, const void* source, uint64_t length)
{
    if (source == 0 || length == 0 || destination < MMU_USER_BASE ||
        destination >= MMU_USER_LIMIT || length > MMU_USER_LIMIT - destination)
        return -1;
    const uint8_t* src = (const uint8_t*)source;
    uint64_t copied = 0;
    while (copied < length)
    {
        uint64_t address = destination + copied;
        uint64_t chunk = MMU_PAGE_SIZE - (address & (MMU_PAGE_SIZE - 1ULL));
        if (chunk > length - copied) chunk = length - copied;
        uint8_t* dst = (uint8_t*)mmu_user_space_pointer(pid, address);
        if (dst == 0) return -2;
        for (uint64_t j = 0; j < chunk; ++j) dst[j] = src[copied + j];
        copied += chunk;
    }
    return 0;
}

int mmu_user_space_finish(int pid)
{
    int slot = user_space_find(pid);
    if (slot < 0) return -1;
    int mapped = 0;
    for (uint64_t i = 0; i < MAX_USER_PAGES; ++i)
        if (user_frames[slot][i] != 0) mapped = 1;
    if (!mapped) return -2;
    user_spaces[slot].ready = 1;
    return 0;
}

int mmu_user_space_activate(int pid)
{
    int slot = user_space_find(pid);
    if (slot < 0 || !user_spaces[slot].ready) return -1;
    load_cr3((uint64_t)as_pml4[slot]);
    active_user_space = slot;
    user_active = 0;
    return 0;
}

int mmu_user_space_deactivate()
{
    if (active_user_space < 0) return 0;
    load_cr3((uint64_t)boot_pml4);
    active_user_space = -1;
    user_active = 0;
    return 0;
}

int mmu_user_space_destroy(int pid)
{
    int slot = user_space_find(pid);
    if (slot < 0) return -1;
    if (active_user_space == slot) (void)mmu_user_space_deactivate();
    for (uint64_t i = 0; i < MAX_USER_PAGES; ++i)
    {
        if (user_frames[slot][i] != 0) (void)free_page((void*)user_frames[slot][i]);
        user_frames[slot][i] = 0;
        user_space_modes[slot][i] = 0;
    }
    for (uint64_t i = 0; i < USER_STACK_PAGES; ++i)
    {
        if (stack_frames[slot][i] != 0) (void)free_page((void*)stack_frames[slot][i]);
        stack_frames[slot][i] = 0;
        user_stack_modes[slot][i] = 0;
    }
    user_spaces[slot].used = 0;
    user_spaces[slot].pid = -1;
    user_spaces[slot].ready = 0;
    return 0;
}

int mmu_user_space_sync_code(int pid, uint64_t start, uint64_t end)
{
    int slot = user_space_find(pid);
    if (slot < 0 || !user_spaces[slot].ready || start < MMU_USER_BASE ||
        end <= start || end > MMU_USER_LIMIT)
        return -1;
    /* x86 keeps instruction and data caches coherent; flush the lines the
       kernel just wrote through the identity map to be explicit. */
    for (uint64_t p = start & ~63ULL; p < ((end + 63ULL) & ~63ULL); p += 64ULL)
    {
        void* host = mmu_user_space_pointer(pid, p < start ? start : p);
        if (host) asm volatile("clflush (%0)" :: "r"(host) : "memory");
    }
    asm volatile("mfence" ::: "memory");
    return 0;
}

void mmu_status()
{
    uart_puts("MMU status\r\n----------\r\n");
    uart_puts("Architecture : x86-64, identity map of first 4 GiB\r\n");
    uart_puts("User VA : 0x07000000 - 0x07700000\r\n");
    uart_puts("Stack : 0x077F0000 - 0x07800000 (64 KiB; guard below)\r\n");
    uart_puts("Isolation : per-process page tables (ring 3)\r\n");
}
