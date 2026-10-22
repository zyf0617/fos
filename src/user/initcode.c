#include "sys.h"
#ifndef LAB4_SCENARIO
#define LAB4_SCENARIO 0
#endif
#ifdef LAB4_TEST
extern long context_probe(void);
static void fail(void)
{
    asm volatile("ld zero, 16(zero)" ::: "memory");
    while (1) ;
}
#endif

__attribute__((section(".text.entry"))) int main(void)
{
#ifdef LAB4_TEST
    if (syscall(SYS_helloworld) != 0 || syscall(SYS_helloworld) != 0)
        fail();
    if (LAB4_SCENARIO == 1 && syscall(12345) != -1)
        fail();
    if (context_probe() != 0)
        fail();
    syscall(98);
    if (LAB4_SCENARIO == 2)
        asm volatile("ld zero, 0(zero)" ::: "memory");
    if (LAB4_SCENARIO == 3)
        asm volatile("sd zero, 0(%0)" : : "r"(0x1000ul) : "memory");
    if (LAB4_SCENARIO == 4)
        asm volatile("ld zero, 0(%0)" : : "r"((1ul << 38) - 8192) : "memory");
    if (LAB4_SCENARIO == 5)
        asm volatile("jalr zero, 0(%0)" : : "r"(0x80200000ul) : "memory");
#else
    syscall(SYS_helloworld);
    syscall(SYS_helloworld);
    int values[5];
    if (syscall(SYS_copyout, values) != 5 || syscall(SYS_copyin, values, 5) != 0)
        goto demo_failed;
    syscall(SYS_copyinstr, "hello, Lab 5");
    unsigned long heap = syscall(SYS_brk, 0);
    if (syscall(SYS_brk, heap + 9 * 4096) != heap + 9 * 4096 ||
        syscall(SYS_brk, heap + 4 * 4096) != heap + 4 * 4096)
        goto demo_failed;
    volatile char large[4 * 4096];
    large[0] = 'a';
    large[sizeof(large) - 1] = 'z';
    if (large[0] != 'a' || large[sizeof(large) - 1] != 'z') goto demo_failed;
    unsigned long area = syscall(SYS_mmap, 0, 3 * 4096);
    if (!area) goto demo_failed;
    *(volatile int *)area = 2026;
    if (*(volatile int *)area != 2026 || syscall(SYS_munmap, area, 3 * 4096) != 0)
        goto demo_failed;
    syscall(SYS_printf, "lab-5 demo PASS\n");
    while (1) asm volatile("" ::: "memory");
demo_failed:
    syscall(SYS_printf, "lab-5 demo FAILED\n");
#endif
    while (1)
        asm volatile("" ::: "memory");
}
