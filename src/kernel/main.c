#include "arch/mod.h"
#include "lib/mod.h"
#include "mem/mod.h"
#include "trap/mod.h"
//_entry在kernel.ld中定义 将后续hart中的启动地址定义为该地址
extern void _entry(void);

// OpenSBI可以选择任意hart作为boot hart，不能假定一定是CPU-0。
// 0=未开始，1=全局初始化中，2=全局初始化完成。
static volatile int init_state = 0;

void main(void)
{
    uint64 hartid = r_tp();

    if (__sync_bool_compare_and_swap(&init_state, 0, 1))
    {
        print_init();
        printf("cpu %d is booting with OpenSBI!\n", (int)hartid);
        pmem_init();
        kvm_init();
        trap_kernel_init();
        __sync_synchronize();
        init_state = 2;

        // 实际boot hart已在运行，通过HSM启动其余所有hart。
        for (uint64 id = 0; id < NCPU; id++)
        {
            if (id == hartid)
                continue;

            sbi_ret_t ret = sbi_hart_start(id, (uint64)_entry, 0);
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

    if (hartid == 0)
        mem_self_test();

    // 先设置当前hart的时钟事件，再安装trap入口并打开全局中断。
    timer_init();
    trap_kernel_inithart();

    while (1)
        asm volatile("wfi");
}
