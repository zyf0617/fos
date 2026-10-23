#include "mod.h"
#include "../syscall/mod.h"
#include "../agent/mod.h"

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
#ifdef PROC_TEST
        if (p->tf->a7 == 200)
        {
            pmem_test_fail_after((int)p->tf->a0);
            p->tf->a0 = 0;
        }
        else
#endif
#ifdef LAB4_TEST
        if (p->tf->a7 == 99)
            p->tf->a0 = timer_get_ticks();
        else if (p->tf->a7 == 98)
        {
            if (lab4_user_uart_interrupts)
                printf("lab-4 user UART interrupt handled\n");
            printf("lab-4 user checks passed\n");
            p->tf->a0 = 0;
        }
        else
#endif
            syscall();
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
    else if ((cause == 13 || cause == 15) &&
             r_stval() >= MMAP_END && r_stval() < USER_STACK_TOP - p->ustack_npage * PGSIZE)
    {
        int64 pages = uvm_ustack_grow(p->pgtbl, p->ustack_npage, r_stval());
        if (pages < 0)
        {
            printf("lab-5 stack growth failed\n");
#if AEGIS
            agent_record(p, AGENT_FAULT, cause, (uint64)-1);
#endif
            proc_return();
        }
        p->ustack_npage = pages;
        printf("lab-5 stack pages=%d\n", (int)pages);
    }
    else
    {
#if AEGIS
        agent_record(p, AGENT_FAULT, cause, (uint64)-1);
#endif
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
    uint64 status = (r_sstatus() & ~(SSTATUS_SPP | (1ul << 18))) | SSTATUS_SPIE;
#if PROC_LIFECYCLE
    // 本阶段只保存整数现场；显式关闭FS/VS，禁止任务共享未保存的扩展寄存器。
    status &= ~((3ul << 13) | (3ul << 9));
#endif
    w_sstatus(status);
    uint64 address = TRAMPOLINE + ((uint64)user_return - (uint64)trampoline);
    ((void (*)(uint64, uint64))address)(TRAPFRAME, MAKE_SATP(p->pgtbl));
    __builtin_unreachable();
}
