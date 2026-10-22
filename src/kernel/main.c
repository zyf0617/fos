#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"
#include "proc/mod.h"
//_entry在kernel.ld中定义 将后续hart中的启动地址定义为该地址
extern void _entry_secondary(void);

// OpenSBI可以选择任意hart作为boot hart，不能假定一定是CPU-0。
// 0=未开始，1=全局初始化中，2=全局初始化完成。
static volatile int init_state = 0;

void main(void)
{
    uint64 hartid = r_tp();

    if (hartid == boot_hartid)
    {
        print_init();
        printf("cpu %d is booting with OpenSBI!\n", (int)hartid);
        pmem_init();
        mmap_init();
        kvm_init();
        trap_kernel_init();
        __sync_synchronize();
        init_state = 2;

        // 实际boot hart已在运行，通过HSM启动其余所有hart。
        for (uint64 id = FIRST_HART; id < NCPU; id++)
        {
            if (id == hartid)
                continue;

            sbi_ret_t ret = sbi_hart_start(id, (uint64)_entry_secondary, 0);
            if (ret.error != SBI_SUCCESS)
            {
                printf("failed to start cpu %d: error %d\n",
                       (int)id, (int)ret.error);
                panic("sbi_hart_start");
            }
        }
    }
    else
    {
        while (init_state != 2)
            ;
        __sync_synchronize();
        printf("cpu %d is booting with OpenSBI!\n", (int)hartid);
    }

    // satp是每个CPU独有的CSR，所以所有CPU都要切换到共享内核页表。
    kvm_inithart();

    if (hartid == boot_hartid)
    {
        mem_self_test();
#ifdef LAB5_TEST
        proc_self_test();
        lab5_self_test();
#endif
#ifdef LAB4_TEST
        lab4_memory_test();
        proc_self_test();
#endif
    }

    // 先设置当前hart的时钟事件，再安装trap入口并打开全局中断。
    timer_init();
    trap_kernel_inithart();
    if (hartid == boot_hartid)
        uart_enable_rx_interrupt();
#ifdef LAB5_TEST
    if (hartid != boot_hartid)
        lab5_pool_worker();
#endif

#if defined(LAB4_TEST) && LAB4_SCENARIO >= 6 && LAB4_SCENARIO <= 8
    if (hartid == boot_hartid)
        print_panic_test(LAB4_SCENARIO);
#endif

#ifndef LAB4_BOOT_USER
#define LAB4_BOOT_USER 1
#endif
    if (hartid == boot_hartid && LAB4_BOOT_USER)
        proc_make_first();

    while (1)
        asm volatile("wfi");
}
