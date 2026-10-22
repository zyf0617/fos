#include "../src/user/sys.h"
#define PAGE 4096ul
#ifdef VISIONFIVE2
#define KERNEL_ADDRESS 0x40200000ul
#else
#define KERNEL_ADDRESS 0x80200000ul
#endif
#define TOP ((1ul << 38) - 8192)
#define MAP_END (TOP - 16ul * 1024 * 1024)
#define MAP_BEGIN (MAP_END - 64ul * 1024 * 1024)
static void check(int good)
{
    if (!good)
    {
        syscall(SYS_printf, "lab-5 USER FAILED\n");
        asm volatile("ld zero, 16(zero)" ::: "memory");
        while (1) ;
    }
}
__attribute__((section(".text.entry"))) int main(void)
{
    check(syscall(SYS_helloworld) == 0);
    check(syscall(SYS_helloworld) == 0);
    check(syscall(0x100000000ul) == -1);
    check(syscall(0xfffffffffffffffful) == -1);
    int values[5];
    check(syscall(SYS_copyout, values) == 5);
    for (int i = 0; i < 5; i++) check(values[i] == i + 1);
    check(syscall(SYS_copyin, values, 5) == 0);
    check(syscall(SYS_copyin, values, 0x100000005ul) == -1);
    check(syscall(SYS_copyout, 0x1000ul) == -1);
    check(syscall(SYS_copyinstr, TOP) == -1);
    check(syscall(SYS_printf, "%d %s are literal\n") == 18);
    check(syscall(SYS_copyinstr, "lab-5 user copy passed") == 0);

    unsigned long heap = syscall(SYS_brk, 0);
    check(heap == 2 * PAGE);
    check(syscall(SYS_brk, heap + 9 * PAGE + 7) == heap + 9 * PAGE + 7);
    volatile char *h = (char *)heap;
    h[0] = 41; h[9 * PAGE + 6] = 42;
    check(h[0] == 41 && h[9 * PAGE + 6] == 42);
    check(syscall(SYS_brk, heap + 4 * PAGE) == heap + 4 * PAGE);
    check(syscall(SYS_brk, heap - 1) == -1);
    check(syscall(SYS_brk, heap) == heap);

    // 从低地址首先访问，确保真正发生跨多页的栈缺页，而非逐页碰巧成功。
    volatile char large[4 * PAGE];
    large[0] = 'w'; large[4 * PAGE - 1] = 'z';
    check(large[0] == 'w' && large[4 * PAGE - 1] == 'z');
    check(syscall(SYS_printf, "lab-5 user heap-stack passed\n") > 0);

    unsigned long m = syscall(SYS_mmap, 0, 3 * PAGE);
    check(m == MAP_BEGIN);
    volatile char *mapped = (char *)m;
    mapped[0] = 17; mapped[3 * PAGE - 1] = 23;
    check(mapped[0] == 17 && mapped[3 * PAGE - 1] == 23);
    check(syscall(SYS_munmap, m + PAGE, PAGE) == 0);
    check(syscall(SYS_munmap, m, 3 * PAGE) == -1);
    check(syscall(SYS_mmap, m + PAGE, PAGE) == m + PAGE);
    check(((char *)m)[PAGE] == 0);
    check(syscall(SYS_munmap, m, 3 * PAGE) == 0);
    check(syscall(SYS_mmap, 0, (1ul << 32) + PAGE) == 0);
    check(syscall(SYS_munmap, 0, PAGE) == -1);
    check(syscall(SYS_printf, "lab-5 USER PASS\n") > 0);
    // 留下动态资源再故障，由内核断言完整回收。
    check(syscall(SYS_brk, heap + 3 * PAGE) == heap + 3 * PAGE);
    check(syscall(SYS_mmap, 0, 2 * PAGE) == MAP_BEGIN);
#if LAB4_SCENARIO == 21
    asm volatile("sd zero, 0(%0)" : : "r"(0x1000ul) : "memory");
#elif LAB4_SCENARIO == 22
    asm volatile("ld zero, 0(%0)" : : "r"(TOP) : "memory");
#elif LAB4_SCENARIO == 23
    asm volatile("jalr zero, 0(%0)" : : "r"(KERNEL_ADDRESS) : "memory");
#else
    asm volatile("ld zero, 0(zero)" ::: "memory");
#endif
    while (1) ;
}
