#include "mod.h"
#include "../agent/mod.h"

#if PROC_LIFECYCLE
static uint64 sys_fork(void) { return proc_fork(); }
static uint64 sys_exit(void) { proc_exit(arg_raw(0)); }
static uint64 sys_wait(void) { return proc_wait(arg_raw(0)); }
static uint64 sys_yield(void) { proc_yield(); return 0; }
static uint64 sys_getpid(void) { return myproc()->pid; }
#endif

static uint64 (*const services[])(void) = {
    [SYS_helloworld] = sys_helloworld,
    [SYS_copyin] = sys_copyin,
    [SYS_copyout] = sys_copyout,
    [SYS_copyinstr] = sys_copyinstr,
    [SYS_brk] = sys_brk,
    [SYS_mmap] = sys_mmap,
    [SYS_munmap] = sys_munmap,
    [SYS_printf] = sys_printf,
#if PROC_LIFECYCLE
    [SYS_fork] = sys_fork,
    [SYS_exit] = sys_exit,
    [SYS_wait] = sys_wait,
    [SYS_yield] = sys_yield,
    [SYS_getpid] = sys_getpid,
#endif
};

void syscall(void)
{
    proc_t *p = myproc();
    assert(p && p->tf, "syscall: no trapframe");
    uint64 number = p->tf->a7;
#if AEGIS
    if (number < sizeof(services) / sizeof(services[0]) && !agent_allowed(p, number))
    {
        p->tf->a0 = number == SYS_mmap ? 0 : (uint64)-1;
        agent_record(p, AGENT_CAP_DENY, number, (uint64)-1);
        return;
    }
#endif
    p->tf->a0 = number < sizeof(services) / sizeof(services[0]) && services[number]
                ? services[number]() : (uint64)-1;
#if AEGIS
    // 记录成功/失败，不将brk/mmap返回的用户地址写入审计。
    bool failed = number == SYS_mmap ? p->tf->a0 == 0 : p->tf->a0 == (uint64)-1;
    agent_record(p, AGENT_SYSCALL, number, failed ? (uint64)-1 : 0);
#endif
}

uint64 arg_raw(int index)
{
    proc_t *p = myproc();
    assert(p && p->tf, "arg_raw: no trapframe");
    switch (index)
    {
    case 0: return p->tf->a0;
    case 1: return p->tf->a1;
    case 2: return p->tf->a2;
    case 3: return p->tf->a3;
    case 4: return p->tf->a4;
    case 5: return p->tf->a5;
    default: panic("arg_raw: illegal argument index");
    }
    __builtin_unreachable();
}

void arg_uint32(int index, uint32 *value)
{
    assert(value != NULL, "arg_uint32: NULL output");
    *value = arg_raw(index);
}
void arg_uint64(int index, uint64 *value)
{
    assert(value != NULL, "arg_uint64: NULL output");
    *value = arg_raw(index);
}
int arg_str(int index, char *buffer, int maxlen)
{
    assert(buffer != NULL, "arg_str: NULL output");
    if (maxlen <= 0) return -1;
    return uvm_copyin_str(myproc()->pgtbl, (uint64)buffer, arg_raw(index), maxlen);
}
