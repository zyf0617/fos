#include "../src/user/sys.h"
#define PAGE 4096ul
static void check(int ok)
{
    if (!ok)
    {
        syscall(SYS_printf, "process USER FAILED\n");
        syscall(SYS_exit, -99);
        for (;;) ;
    }
}
static void finish(long status)
{
    syscall(SYS_exit, status);
    for (;;) ;
}

__attribute__((section(".text.entry"))) int main(void)
{
    long status = 0;
    check(syscall(SYS_getpid) == 0 && syscall(SYS_wait, &status) == -1);
    check(syscall(SYS_brk, 3 * PAGE) == 3 * PAGE);
    volatile long *heap = (long *)(2 * PAGE);
    heap[0] = 41;
    unsigned long map = syscall(SYS_mmap, 0, PAGE);
    check(map != 0);
    ((volatile long *)map)[0] = 77;
#if LAB4_SCENARIO >= 31 && LAB4_SCENARIO <= 33
    check(syscall(SYS_fork) == -1 && syscall(SYS_wait, 0) == -1 &&
          heap[0] == 41 && ((volatile long *)map)[0] == 77);
    syscall(SYS_printf, "process fork policy rejection PASS\n");
    finish(0);
#endif
    check(syscall(0x100000008ul) == -1);
    long pid = syscall(SYS_fork);
    check(pid >= 0);
    if (!pid)
    {
        check(syscall(SYS_getpid) > 0 && heap[0] == 41 && ((volatile long *)map)[0] == 77);
        heap[0] = 99;
        ((volatile long *)map)[0] = 88;
        check(syscall(SYS_yield) == 0);
        finish(0x123456789abcdefL);
    }
    // 无僵尸时wait阻塞；子进程yield后须能恢复同一内核上下文。
    check(syscall(SYS_wait, &status) == pid && status == 0x123456789abcdefL &&
          heap[0] == 41 && ((volatile long *)map)[0] == 77);
    check(syscall(SYS_wait, 0) == -1);
    syscall(SYS_printf, "process fork isolation and blocking wait PASS\n");

    pid = syscall(SYS_fork);
    check(pid >= 0);
    if (!pid) finish(-7);
    check(syscall(SYS_yield) == 0);
    // 子进程已退出：坏指针不得吞掉状态，随后可重试。
    check(syscall(SYS_wait, 1) == -1 && syscall(SYS_wait, PAGE) == -1 &&
          syscall(SYS_wait, (1ul << 38) - 8192) == -1 &&
          syscall(SYS_wait, 3 * PAGE - 4) == -1 &&
          syscall(SYS_wait, &status) == pid && status == -7);
    syscall(SYS_printf, "process wait retry PASS\n");

    pid = syscall(SYS_fork);
    check(pid >= 0);
    if (!pid)
    {
        asm volatile("ld zero, 0(zero)" ::: "memory");
        finish(-99);
    }
    check(syscall(SYS_wait, &status) == pid && status == -1);
    syscall(SYS_printf, "process child fault reap PASS\n");

    pid = syscall(SYS_fork);
    check(pid >= 0);
    if (!pid)
    {
        asm volatile("fmv.d.x ft0, zero" ::: "ft0");
        finish(-99);
    }
    check(syscall(SYS_wait, &status) == pid && status == -1);
    syscall(SYS_printf, "process unsupported FP fault PASS\n");

    long ids[3];
    for (int i = 0; i < 3; i++)
    {
        ids[i] = syscall(SYS_fork);
        check(ids[i] >= 0);
        if (!ids[i]) finish(10 + i);
    }
    check(syscall(SYS_fork) == -1); // 4槽均占用，不能覆盖已有进程。
    int seen = 0;
    for (int i = 0; i < 3; i++)
    {
        long reaped = syscall(SYS_wait, &status);
        for (int j = 0; j < 3; j++)
            if (reaped == ids[j]) { check(status == 10 + j && !(seen & (1 << j))); seen |= 1 << j; }
    }
    check(seen == 7 && syscall(SYS_wait, 0) == -1);
    syscall(SYS_printf, "process slot exhaustion PASS\n");

    int failures = 0, succeeded = 0;
    for (int n = 0; n < 64; n++)
    {
        syscall(200, n); // 仅PROC_TEST镜像提供故障注入入口。
        pid = syscall(SYS_fork);
        if (pid < 0) { failures++; continue; }
        if (!pid) finish(23);
        check(syscall(SYS_wait, &status) == pid && status == 23);
        succeeded = 1;
        break;
    }
    check(succeeded && failures > 8 && heap[0] == 41 && ((volatile long *)map)[0] == 77);
    syscall(SYS_printf, "process actual fork OOM rollback PASS\n");
    // 多轮复用：PID不重用、回收后的页预算和节点可继续fork。
    long last = pid;
    for (int i = 0; i < 12; i++)
    {
        pid = syscall(SYS_fork);
        check(pid >= 0);
        if (!pid) finish(i);
        check(pid > last && syscall(SYS_wait, &status) == pid && status == i);
        last = pid;
    }
    syscall(SYS_printf, "process repeated reuse PASS\n");
    pid = syscall(SYS_fork);
    check(pid >= 0);
    if (!pid) finish(0);
    syscall(SYS_yield); // 留下一个未wait的僵尸，父退出时应自动回收槽位。
    pid = syscall(SYS_fork);
    check(pid >= 0);
    if (!pid)
    {
        syscall(SYS_yield);
        long grandchild = syscall(SYS_fork);
        check(grandchild >= 0);
        if (!grandchild) finish(42);
        check(syscall(SYS_wait, &status) == grandchild && status == 42);
        syscall(SYS_printf, "process orphan survived PASS\n");
        finish(0);
    }
    syscall(SYS_printf, "process USER PASS\n");
    finish(0); // 父先退出；子仍能运行，最终由内核自动回收。
}
