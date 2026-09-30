#pragma once
#include "../lock/type.h"

// platform-level interrupt controller(PLIC)
#define PLIC_BASE 0x0c000000ul
#define PLIC_SIZE 0x04000000ul
#define PLIC_PRIORITY(id) (PLIC_BASE + (id) * 4)
#define PLIC_PENDING (PLIC_BASE + 0x1000)
#define PLIC_MENABLE(hart) (PLIC_BASE + 0x2000 + (hart) * 0x100)
#define PLIC_SENABLE(hart) (PLIC_BASE + 0x2080 + (hart) * 0x100)
#define PLIC_MPRIORITY(hart) (PLIC_BASE + 0x200000 + (hart) * 0x2000)
#define PLIC_SPRIORITY(hart) (PLIC_BASE + 0x201000 + (hart) * 0x2000)
#define PLIC_MCLAIM(hart) (PLIC_BASE + 0x200004 + (hart) * 0x2000)
#define PLIC_SCLAIM(hart) (PLIC_BASE + 0x201004 + (hart) * 0x2000)

// scause的最高位区分中断和异常，其余位是cause code。
#define SCAUSE_INTERRUPT (1ull << 63)
#define SCAUSE_CODE_MASK (~SCAUSE_INTERRUPT)
#define SCAUSE_S_TIMER 5
#define SCAUSE_S_EXTERNAL 9

// QEMU virt的mtime频率为10MHz，每100ms产生一次时钟中断。
#define TIMER_INTERVAL 1000000ull

typedef struct timer
{
    uint64 ticks;
    spinlock_t lk; // 保护ticks，多核读写必须持有该锁。
} timer_t;
