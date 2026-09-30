#include "mod.h"

// 所有hart共享系统滴答，但只由CPU-0更新。
static timer_t system_timer;

void timer_create(void)
{
    system_timer.ticks = 0;
    spinlock_init(&system_timer.lk, "timer.ticks");
}

// stimecmp属于hart本地状态，每个CPU都必须设置自己的首次事件。
void timer_init(void)
{
    sbi_ret_t ret = sbi_set_timer(r_time() + TIMER_INTERVAL);
    if (ret.error != SBI_SUCCESS)
        panic("timer_init: sbi_set_timer failed");

    w_sie(r_sie() | SIE_STIE);
}

void timer_update(void)
{
    uint64 ticks;

    assert(mycpuid() == 0, "timer_update: only hart 0 may update ticks");
    spinlock_acquire(&system_timer.lk);
    system_timer.ticks++;
    ticks = system_timer.ticks;
    spinlock_release(&system_timer.lk);

    // 前三滴用于快速回归测试，之后降低输出频率，避免干扰UART输入。
    if (ticks <= 3 || ticks % 10 == 0)
        printf("ticks = %d\n", (int)ticks);
    if (ticks == 3)
        printf("lab-3 timer interrupt test passed\n");
}

uint64 timer_get_ticks(void)
{
    uint64 ticks;

    spinlock_acquire(&system_timer.lk);
    ticks = system_timer.ticks;
    spinlock_release(&system_timer.lk);
    return ticks;
}
