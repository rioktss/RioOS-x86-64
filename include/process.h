/*
 * MIT License
 *
 * Copyright (c) 2026 Rio okta kusuma
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files, to deal in the Software
 * without restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"

enum ProcessState
{
    PROCESS_UNUSED = 0,
    PROCESS_READY = 1,
    PROCESS_RUNNING = 2,
    PROCESS_SLEEPING = 3,
    PROCESS_ZOMBIE = 4
};

/* EL0 architectural state retained at an exception boundary. This is metadata
   for a resumable process context; it is distinct from kernel-thread SP state. */
struct ProcessCpuContext
{
    uint64_t x[31];
    uint64_t elr;
    uint64_t spsr;
    uint64_t user_sp;
};

enum ProcessKind
{
    PROCESS_KERNEL = 0,
    PROCESS_USER = 1
};

void process_init();
int process_create(const char* name);
/* process_destroy returns -2 for built-ins, -3 for a RUNNING record, -1 if missing. */
int process_destroy(int pid);
int process_count();
void process_list();
int process_set_state(int pid, int state);
int process_get_state(int pid);
int process_configure_user(int pid, uint64_t low, uint64_t high);
int process_mark_exit(int pid, int code);
int process_is_user(int pid);
uint64_t process_user_low(int pid);
uint64_t process_user_high(int pid);
int process_exit_code(int pid);
int process_save_context(int pid, const ProcessCpuContext* context);
int process_load_context(int pid, ProcessCpuContext* context);
int process_context_valid(int pid);
int process_pid_at(int slot);

#endif
