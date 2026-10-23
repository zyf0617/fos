#include "../src/user/sys.h"
extern long rr_context(unsigned long duration, unsigned long seed);
static void check(int ok)
{
    if (!ok)
    {
        syscall(SYS_printf, "rr USER FAILED\n");
        syscall(SYS_exit, -99);
        for (;;) ;
    }
}
static void worker(int role)
{
    syscall(SYS_printf, role == 1 ? "rr START A\n" : "rr START B\n");
    // 循环内只有整数运算、栈访问和rdtime，不进行ecall/yield。
    check(rr_context(10000000ul, role * 256ul) == 0);
    syscall(SYS_printf, role == 1 ? "rr DONE A\n" : "rr DONE B\n");
    syscall(SYS_exit, role);
    for (;;) ;
}
__attribute__((section(".text.entry"))) int main(void)
{
    long a = syscall(SYS_fork);
    check(a >= 0);
    if (!a) worker(1);
    long b = syscall(SYS_fork);
    check(b >= 0);
    if (!b) worker(2);
    long status = 0, seen = 0;
    for (int i = 0; i < 2; i++)
    {
        long pid = syscall(SYS_wait, &status);
        if (pid == a) { check(status == 1 && !(seen & 1)); seen |= 1; }
        else { check(pid == b && status == 2 && !(seen & 2)); seen |= 2; }
    }
    check(seen == 3 && syscall(SYS_wait, 0) == -1);
    syscall(SYS_printf, "rr USER PASS\n");
    syscall(SYS_exit, 0);
    for (;;) ;
}
