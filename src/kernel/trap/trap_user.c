#include "mod.h"
#include "../../user/syscall_num.h"

extern char trampoline[], user_vector[], user_return[], kernel_vector[];
#ifdef LAB4_TEST
extern uint64 lab4_user_uart_interrupts;
#endif

void trap_user_handler(void)
{
    assert(!(r_sstatus() & SSTATUS_SPP) && !intr_get(),
           "trap_user_handler: invalid privilege state");
    w_stvec((uint64)kernel_vector);
    proc_t *p = myproc();
    assert(p != NULL, "trap_user_handler: no process");
    p->tf->user_to_kern_epc = r_sepc();
    uint64 cause = r_scause();
    if (cause == 8)
    {
        p->tf->user_to_kern_epc += 4;
        if (p->tf->a7 == SYS_helloworld)
        {
            printf("proczero: hello world!\n");
            p->tf->a0 = 0;
        }
#ifdef LAB4_TEST
        else if (p->tf->a7 == 99)
            p->tf->a0 = timer_get_ticks();
        else if (p->tf->a7 == 98)
        {
            if (lab4_user_uart_interrupts)
                printf("lab-4 user UART interrupt handled\n");
            printf("lab-4 user checks passed\n");
            p->tf->a0 = 0;
        }
#endif
        else
            p->tf->a0 = (uint64)-1;
    }
    else if (cause == (SCAUSE_INTERRUPT | SCAUSE_S_TIMER))
    {
        timer_interrupt_handler();
#ifdef LAB4_TEST
        if (timer_get_ticks() == 3)
            printf("lab-4 user timer interrupt test passed\n");
#endif
    }
    else if (cause == (SCAUSE_INTERRUPT | SCAUSE_S_EXTERNAL))
    {
        external_interrupt_handler();
    }
    else
    {
        printf("lab-4 user fault: cause=%p epc=%p stval=%p\n",
               cause, p->tf->user_to_kern_epc, r_stval());
        proc_return();
    }
    trap_user_return();
}

void trap_user_return(void)
{
    intr_off();
    proc_t *p = myproc();
    assert(p != NULL && p->tf != NULL, "trap_user_return: no process");
    trapframe_t *tf = p->tf;
    tf->user_to_kern_satp = r_satp();
    tf->user_to_kern_sp = p->kstack + PGSIZE;
    tf->user_to_kern_trapvector = (uint64)trap_user_handler;
    tf->user_to_kern_hartid = r_tp();
    w_stvec(TRAMPOLINE + ((uint64)user_vector - (uint64)trampoline));
    w_sepc(tf->user_to_kern_epc);
    w_sstatus((r_sstatus() & ~(SSTATUS_SPP | (1ul << 18))) | SSTATUS_SPIE);
    uint64 address = TRAMPOLINE + ((uint64)user_return - (uint64)trampoline);
    ((void (*)(uint64, uint64))address)(TRAPFRAME, MAKE_SATP(p->pgtbl));
    __builtin_unreachable();
}
