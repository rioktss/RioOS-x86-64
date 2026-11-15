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

#include "smp.h"

static uint32_t current_cpu_id = 0;
/* Keep this at one until secondary cores are explicitly brought online. */
static uint32_t online_cpus = 1;
static int smp_enabled_flag = 0;

void smp_init()
{
    /* Single boot CPU (id 0); secondary cores are not started on x86-64. */
    current_cpu_id = 0;
    online_cpus = 1;
    smp_enabled_flag = 0;
}

uint32_t smp_cpu_id() { return current_cpu_id; }
uint32_t smp_online_cpu_count() { return online_cpus; }
int smp_is_enabled() { return smp_enabled_flag; }

void smp_spinlock_init(SmpSpinlock* lock)
{
    if (lock == 0) return;
    __atomic_store_n(&lock->value, 0U, __ATOMIC_RELAXED);
}

void smp_spin_lock(SmpSpinlock* lock)
{
    if (lock == 0) return;
    while (__atomic_exchange_n(&lock->value, 1U, __ATOMIC_ACQUIRE) != 0U)
    {
#ifndef HOST_TEST
        asm volatile("pause" ::: "memory");
#endif
    }
}

int smp_spin_try_lock(SmpSpinlock* lock)
{
    if (lock == 0) return 0;
    uint32_t expected = 0U;
    return __atomic_compare_exchange_n(&lock->value, &expected, 1U, 0,
                                      __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ? 1 : 0;
}

void smp_spin_unlock(SmpSpinlock* lock)
{
    if (lock == 0) return;
    __atomic_store_n(&lock->value, 0U, __ATOMIC_RELEASE);
}

#ifdef HOST_TEST
void smp_test_set_cpu_id(uint32_t id) { current_cpu_id = id & 0xFFU; }
#endif
