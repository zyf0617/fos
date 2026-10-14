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
#else
    syscall(SYS_helloworld);
    syscall(SYS_helloworld);
#endif
    while (1)
        asm volatile("" ::: "memory");
}
