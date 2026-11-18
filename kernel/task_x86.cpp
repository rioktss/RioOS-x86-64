#include "task.h"

/* The current x86-64 port has a ring-3 ELF runner, but not yet the per-process
   kernel-thread scheduler required by AArch64 job control/signals. Never report
   a fake child process: return -ENOTSUP until the x86 task context is ported. */
static const int TASK_ENOTSUP = -95;

int task_spawn(const char*, const char*, int, int, int, int) { return TASK_ENOTSUP; }
int task_fork(ExceptionFrame*) { return TASK_ENOTSUP; }
int task_exec(ExceptionFrame*, const char*, const char*) { return TASK_ENOTSUP; }
extern "C" void task_exit_current(int, int) { for (;;) asm volatile("cli; hlt"); }
int task_wait(int, int, int*, int) { return TASK_ENOTSUP; }
int task_kill(int, int) { return TASK_ENOTSUP; }
int task_kill_info(int, int, int, int, uint64_t) { return TASK_ENOTSUP; }
int task_deliver_fault(ExceptionFrame*, int, uint64_t) { return TASK_ENOTSUP; }
int task_reap(int) { return 0; }
int task_reap_orphans(int, void (*)(int, int, int)) { return 0; }
int task_running_count() { return 0; }
extern "C" void task_deliver_signals(ExceptionFrame*) {}
int task_sigreturn(ExceptionFrame*) { return TASK_ENOTSUP; }
extern "C" void task_context_enter(int) {}
int task_console_key(int, char) { return 0; }
int task_wait_foreground(int, int) { return TASK_ENOTSUP; }
extern "C" void user_resume(const void*, uint64_t) {}
uint64_t thread_stack_top_current() { return 0; }
