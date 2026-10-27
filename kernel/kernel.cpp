#include "uart.h"
#include "memory.h"
#include "fs.h"
#include "vfs.h"
#include "process.h"
#include "timer.h"
#include "keyboard.h"
#include "shell.h"
#include "scheduler.h"
#include "thread.h"
#include "exceptions.h"
#include "syscall.h"
#include "mmu.h"
#include "storage.h"
#include "netstack.h"
#include "interrupt.h"
#include "page_alloc.h"
#include "security.h"
#include "smp.h"
#include "version.h"
#include "ui.h"

#ifdef MYKERNEL_X86_64
extern "C" void x86_uart_init();
#endif

extern "C" void kernel_main()
{
#ifdef MYKERNEL_X86_64
    x86_uart_init();
#endif
    uart_puts(
        "\r\n"
        "============================\r\n"
        "        RioOS v" MYKERNEL_VERSION_STRING "\r\n"
        "============================\r\n"
#ifdef MYKERNEL_X86_64
        "x86-64 Bare-Metal Kernel\r\n"
        "QEMU x86_64 machine\r\n"
#else
        "ARM64 Bare-Metal Kernel\r\n"
        "QEMU virt machine\r\n"
#endif
        "\r\n"
    );

    /* Read the platform CPU identifier. Secondary-core startup remains disabled. */
    smp_init();
    uart_puts("[0] CPU topology: boot CPU affinity-0 = ");
    {
        char cpu_id_text[4];
        uint32_t cpu_id = smp_cpu_id();
        int pos = 0;
        if (cpu_id == 0U) cpu_id_text[pos++] = '0';
        while (cpu_id > 0U && pos < 3)
        {
            cpu_id_text[pos++] = (char)('0' + (cpu_id % 10U));
            cpu_id /= 10U;
        }
        while (pos > 0) uart_putc(cpu_id_text[--pos]);
    }
    uart_puts("; online CPUs = 1; SMP disabled by design.\r\n");

    uart_puts("[1] Initializing memory...\r\n");
    memory_init();
    uart_puts("[2] Memory initialized.\r\n");

    /* Physical page pool (4 MiB + alignment slack) carved from the heap. */
    {
        void* pool = kmalloc(4ULL * 1024ULL * 1024ULL + PAGE_SIZE_BYTES);

        if (pool == 0 || page_alloc_init(pool, 4ULL * 1024ULL * 1024ULL + PAGE_SIZE_BYTES) != 0)
            uart_puts("Page allocator unavailable.\r\n");
        else
            uart_puts("Page allocator ready.\r\n");
    }

    /* Install a real EL1 vector before enabling MMU. If MMU setup faults,
       the exception path can report it instead of silently hanging. */
    uart_puts("[3] Initializing exception vectors...\r\n");
    exceptions_init();

    /* The counter is needed by every timeout path, so bring it up early. */
    uart_puts("[4] Initializing timer...\r\n");
    timer_init();
    /* Timer starts OFF. Login/authentication never starts timer IRQs. */

    uart_puts("[5] Initializing MMU...\r\n");
    mmu_init();

#ifdef MYKERNEL_X86_64
    uart_puts("[6] Initializing storage / ATA PIO...\r\n");
#else
    uart_puts("[6] Initializing storage / VirtIO block...\r\n");
#endif
    storage_init();

    uart_puts("[7] Initializing network...\r\n");
    net_init();

    uart_puts("[8] Initializing filesystem...\r\n");
    fs_init();

    uart_puts("[9] Initializing VFS...\r\n");
    vfs_init();

    uart_puts("[10] Initializing process manager...\r\n");
    process_init();
    security_init();

    uart_puts("[11] Initializing keyboard...\r\n");
    keyboard_init();

    uart_puts("[12] Initializing scheduler...\r\n");
    scheduler_init();
    thread_init();
    uart_puts("Cooperative thread subsystem initialized (timer preemption remains off).\r\n");

    uart_puts("[13] Initializing interrupt controller...\r\n");
    interrupt_init();

    uart_puts("[14] Initializing syscall...\r\n");
    syscall_init();

    /* Start a low-rate PIT tick before entering the shell. The PIC IRQ0 line
       and CPU interrupt flag must both be enabled; otherwise the idle path's
       HLT can never wake and the kernel appears frozen. */
#ifdef MYKERNEL_X86_64
    timer_set_sched_tick(0);
    timer_start_periodic(100U);
    interrupt_timer_line_enable();
    interrupt_enable();
    uart_puts("[15] PIT IRQ0 enabled at 100 Hz; CPU interrupts enabled.\r\n");
#else
    uart_puts("[15] Timer IRQ prepared (off by default; use 'irq' to test stages B-D).\r\n");
#endif

uart_puts(
    "\r\n"
    "RioOS initialization complete.\r\n"
    "Welcome to RioOS!\r\n"
    "Type 'help' to get started.\r\n"
    "\r\n"
);

/* Start either the graphical UI or the plain command shell.
   Text-only builds are useful for terminal-first Live USB images and serial
   console testing. The default build preserves the existing graphical UI. */
#ifndef RIOOS_TEXT_ONLY
ui_start();
#else
    uart_puts("Text-only mode enabled; graphical UI skipped.\r\n");
#endif
shell();

    while (1)
#ifdef MYKERNEL_X86_64
        asm volatile("sti; hlt" ::: "memory");
#else
        asm volatile("wfe");
#endif
}
