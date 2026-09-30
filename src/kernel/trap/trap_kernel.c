#include "mod.h"

static const char *const interrupt_info[] = {
    "U-mode software interrupt",
    "S-mode software interrupt",
    "reserved interrupt",
    "M-mode software interrupt",
    "U-mode timer interrupt",
    "S-mode timer interrupt",
    "reserved interrupt",
    "M-mode timer interrupt",
    "U-mode external interrupt",
    "S-mode external interrupt",
    "reserved interrupt",
    "M-mode external interrupt",
};

static const char *const exception_info[] = {
    "Instruction address misaligned",
    "Instruction access fault",
    "Illegal instruction",
    "Breakpoint",
    "Load address misaligned",
    "Load access fault",
    "Store/AMO address misaligned",
    "Store/AMO access fault",
    "Environment call from U-mode",
    "Environment call from S-mode",
    "reserved exception",
    "Environment call from M-mode",
    "Instruction page fault",
    "Load page fault",
    "reserved exception",
    "Store/AMO page fault",
};

extern void kernel_vector(void);

static const char *cause_description(const char *const *descriptions,
                                     uint64 count, uint64 cause)
{
    if (cause < count)
        return descriptions[cause];
    return "unknown cause";
}

void trap_kernel_init(void)
{
    plic_init();
    timer_create();
}

void trap_kernel_inithart(void)
{
    // 全局SIE开启前必须先安装入口和配置各中断源。
    intr_off();
    w_stvec((uint64)kernel_vector);
    plic_inithart();
    w_sie(r_sie() | SIE_SEIE);
    intr_on();
}

void trap_kernel_handler(void)
{
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();
    uint64 stval = r_stval();
    uint64 cause = scause & SCAUSE_CODE_MASK;

    assert((sstatus & SSTATUS_SPP) != 0,
           "trap_kernel_handler: trap not from S-mode");
    assert(intr_get() == 0,
           "trap_kernel_handler: interrupts unexpectedly enabled");

    if ((scause & SCAUSE_INTERRUPT) != 0)
    {
        switch (cause)
        {
        case SCAUSE_S_TIMER:
            timer_interrupt_handler();
            break;
        case SCAUSE_S_EXTERNAL:
            external_interrupt_handler();
            break;
        default:
            printf("unexpected interrupt: %s\n",
                   cause_description(interrupt_info,
                                     sizeof(interrupt_info) / sizeof(interrupt_info[0]),
                                     cause));
            printf("cause = %p, sepc = %p, stval = %p\n",
                   cause, sepc, stval);
            panic("trap_kernel_handler: unhandled interrupt");
        }
    }
    else
    {
        printf("unexpected exception: %s\n",
               cause_description(exception_info,
                                 sizeof(exception_info) / sizeof(exception_info[0]),
                                 cause));
        printf("cause = %p, sepc = %p, stval = %p\n",
               cause, sepc, stval);
        panic("trap_kernel_handler: unhandled exception");
    }

    // 当前只处理异步中断，返回被打断的原指令。
    w_sepc(sepc);
}

void external_interrupt_handler(void)
{
    int irq = plic_claim();

    if (irq == UART_IRQ)
    {
        uart_intr();
    }
    else if (irq != 0)
    {
        printf("unexpected external irq %d\n", irq);
    }

    // claim返回0表示已无待处理中断，不应向complete寄存器写0。
    if (irq != 0)
        plic_complete(irq);
}

void timer_interrupt_handler(void)
{
    sbi_ret_t ret = sbi_set_timer(r_time() + TIMER_INTERVAL);
    if (ret.error != SBI_SUCCESS)
        panic("timer_interrupt_handler: sbi_set_timer failed");

    if (mycpuid() == 0)
        timer_update();
}
