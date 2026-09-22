#include "../arch/mod.h"

// 每个CPU在运行操作系统时需要一个初始的函数栈
__attribute__((aligned(16))) uint8 CPU_stack[4096 * NCPU];

extern void main();

void start(uint64 hartid)
{
    // OpenSBI已在S-mode进入内核。HSM启动hart时satp也为0。
    w_satp(0);
    sfence_vma();

    // OpenSBI通过a0传入hartid，内核统一用tp保存当前CPU编号。
    w_tp(hartid);

    // 进入main函数
    main();

    // main不应返回，作为防御性处理让当前CPU停在低功耗状态。
    while (1)
        asm volatile("wfi");
}
