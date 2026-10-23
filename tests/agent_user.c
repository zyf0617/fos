#include "../src/user/sys.h"
#define PAGE 4096ul
static void check(int good)
{
    if (!good)
    {
        syscall(SYS_printf, "aegis USER FAILED\n");
        asm volatile("ld zero, 16(zero)" ::: "memory");
        while (1) ;
    }
}

__attribute__((section(".text.entry"))) int main(void)
{
    int values[5] = {7, 7, 7, 7, 7};
    check(syscall(SYS_copyout, values) == -1);
    for (int i = 0; i < 5; i++) check(values[i] == 7);
    check(syscall(SYS_copyin, values, 5) == 0);
    check(syscall(0x100000002ul, values) == -1);
    check(syscall(SYS_printf, "aegis capability deny PASS\n") > 0);
    unsigned long map = syscall(SYS_mmap, 0, 2 * PAGE);
    check(map != 0);
    ((volatile char *)map)[0] = 42;
    check(syscall(SYS_mmap, 0, PAGE) == 0);
    check(syscall(SYS_brk, 3 * PAGE) == -1);
    check(syscall(SYS_brk, 0) == 2 * PAGE);
    check(syscall(SYS_munmap, map, PAGE) == 0);
    check(syscall(SYS_brk, 3 * PAGE) == 3 * PAGE);
    check(((volatile char *)(2 * PAGE))[0] == 0);
    check(syscall(SYS_brk, 2 * PAGE) == 2 * PAGE);
    check(syscall(SYS_mmap, map, PAGE) == map);
    check(((volatile char *)map)[0] == 0);
    check(syscall(SYS_printf, "aegis quota reuse PASS\naegis USER PASS\n") > 0);
    // 预算已满，真实栈缺页不得额外分配；失败应退出并清理全部资源。
    unsigned long fault = (1ul << 38) - 8192 - 5 * PAGE;
    asm volatile("sd zero, 0(%0)" : : "r"(fault) : "memory");
    while (1) ;
}
