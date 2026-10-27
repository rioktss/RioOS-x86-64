#include "smp.h"
static uint32_t cpus=1;
void smp_init() { cpus=1; }
uint32_t smp_cpu_id() { return 0; }
uint32_t smp_online_cpu_count() { return cpus; }
int smp_is_enabled() { return 0; }
void smp_spinlock_init(SmpSpinlock* l) { if(l) __atomic_store_n(&l->value,0U,__ATOMIC_RELAXED); }
void smp_spin_lock(SmpSpinlock* l) { if(!l)return; while(__atomic_exchange_n(&l->value,1U,__ATOMIC_ACQUIRE)) asm volatile("pause"); }
int smp_spin_try_lock(SmpSpinlock* l) { if(!l)return 0; uint32_t e=0; return __atomic_compare_exchange_n(&l->value,&e,1U,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?1:0; }
void smp_spin_unlock(SmpSpinlock* l) { if(l) __atomic_store_n(&l->value,0U,__ATOMIC_RELEASE); }
