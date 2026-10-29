# Timer IRQ (v1.13.0)

Up to 1.12.0 the timer interrupt was kept off during normal boot and while a
ring 3 program ran. From 1.13.0 it is a real 100 Hz system tick on x86-64.

## What changed

- `kernel_main()` (step 15) unmasks IRQ0 on the 8259 PIC, programs the 8254 PIT for **100 Hz periodic**, enables the scheduler tick and executes `sti`. The tick is live in the kernel shell.
- `user_enter()` returns to ring 3 with `RFLAGS.IF` set, so the tick keeps firing while `nano`, `calc` or any ELF runs. The CPU switches to the TSS `rsp0` stack on interrupts from ring 3; the IRQ0 stub saves registers, calls `x86_irq_dispatch()` and returns with `iretq`.
- `user_execute()` no longer stops the timer. IRQs are masked only while the kernel builds the process address space and during syscall entry (SYSCALL masks IF through `IA32_FMASK`); `user_execute()` restores the previous mask afterwards and re-arms the 100 Hz tick if something stopped it.
- `interrupt_idle()` executes `hlt` while the periodic tick runs (so the shell waits without spinning) and `pause` otherwise.
- `irq on` restarts the tick; `irq selftest`, `irq oneshot`, `irq stagec/staged` run their staged tests and restore the 100 Hz tick. `irq off` stops the tick until `irq on`.
- `calc` prints `Timer IRQ is active (100 Hz).`

## What did not change

The scheduler tick only counts ticks (`scheduler_tick()`); there is no
preemptive switching between processes, so ring 3 programs run to completion
from the shell. Handlers never print or allocate.

## Verify on QEMU

```
irq status        # CPU IRQ: unmasked, Timer mode: periodic, Timer Hz: 100
tasks             # "Sched ticks" keeps increasing
exec /bin/calc.elf  # then `irq status` after exiting: ticks continued during ring 3
irq selftest      # 4/4 passed, then status shows the periodic tick again
```
Host tests cannot execute interrupts; these are runtime checks.
