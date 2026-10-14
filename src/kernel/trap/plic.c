#include "mod.h"

// PLIC优先级寄存器是多核共享状态，只由boot hart初始化一次。
void plic_init(void)
{
    *(volatile uint32 *)PLIC_PRIORITY(UART_IRQ) = 1;
}

// 为当前hart的S-mode context开启UART中断，并接受所有正优先级事件。
void plic_inithart(void)
{
    int hartid = mycpuid();
    assert(hartid >= 0 && hartid < NCPU, "plic_inithart: invalid hart id");

    *(volatile uint32 *)PLIC_SENABLE(hartid) = hartid == 0 ? (1u << UART_IRQ) : 0;
    *(volatile uint32 *)PLIC_SPRIORITY(hartid) = 0;
}

int plic_claim(void)
{
    int hartid = mycpuid();
    return *(volatile uint32 *)PLIC_SCLAIM(hartid);
}

void plic_complete(int irq)
{
    int hartid = mycpuid();
    *(volatile uint32 *)PLIC_SCLAIM(hartid) = (uint32)irq;
}
