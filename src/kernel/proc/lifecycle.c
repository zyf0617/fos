#include "mod.h"
#include "../trap/mod.h"
#include "../agent/mod.h"
#if PROC_LIFECYCLE

// 单hart拥有；每次状态转换均关中断，无锁跨越swtch。
enum state { FREE, RUNNABLE, RUNNING, WAITING, ZOMBIE };
typedef struct process_slot {
    proc_t proc;
    enum state state;
    int pid;
    struct process_slot *parent;
    uint64 status;
} process_slot_t;
static process_slot_t slots[PROC_SLOTS];
static int next_pid;
extern void swtch(context_t *, context_t *);

static void owner(void)
{
    assert((uint64)mycpuid() == boot_hartid && !intr_get() && !mycpu()->noff,
           "process: invalid owner/interrupt/lock state");
}

static process_slot_t *current(void)
{
    owner();
    for (int i = 0; i < PROC_SLOTS; i++)
        if (&slots[i].proc == myproc() && slots[i].state == RUNNING)
            return &slots[i];
    panic("process: no running slot");
    __builtin_unreachable();
}

static void suspend(process_slot_t *s, enum state state)
{
    owner();
    assert(s->state == RUNNING && &s->proc == myproc(), "process: invalid suspension");
    s->state = state;
    swtch(&s->proc.ctx, &mycpu()->ctx);
    owner();
    assert(s->state == RUNNING && &s->proc == myproc(), "process: invalid resume");
}

uint64 proc_fork(void)
{
    process_slot_t *parent = current();
    if (next_pid == 0x7fffffff) return (uint64)-1;
    for (int i = 0; i < PROC_SLOTS; i++)
    {
        process_slot_t *child = &slots[i];
        if (child->state != FREE) continue;
#ifdef PROC_TEST
        uint32 k = pmem_available(true), u = pmem_available(false), nodes = mmap_available();
        uint64 pages = parent->proc.agent_pages;
#endif
        int result = proc_clone_resources(&child->proc, &parent->proc, next_pid);
#ifdef PROC_TEST
        pmem_test_fail_after(-1);
        if (result)
            assert(pmem_available(true) == k && pmem_available(false) == u &&
                   mmap_available() == nodes && parent->proc.agent_pages == pages,
                   "process: failed fork leaked resources");
#endif
        if (result) return (uint64)-1;
        child->pid = next_pid++;
        child->parent = parent;
        child->proc.kstack = KSTACK(i);
        memset((void *)KSTACK(i), 0, PGSIZE);
        child->proc.ctx.ra = (uint64)trap_user_return;
        child->proc.ctx.sp = KSTACK(i) + PGSIZE;
        child->state = RUNNABLE; // 全部资源和运行上下文准备好后才发布。
        return child->pid;
    }
#ifdef PROC_TEST
    pmem_test_fail_after(-1);
#endif
    return (uint64)-1;
}

void proc_yield(void)
{
    suspend(current(), RUNNABLE);
}

uint64 proc_wait(uint64 status)
{
    process_slot_t *parent = current();
    for (;;)
    {
        bool children = false;
        for (int i = 0; i < PROC_SLOTS; i++)
        {
            process_slot_t *child = &slots[i];
            if (child->state == FREE || child->parent != parent) continue;
            children = true;
            if (child->state != ZOMBIE) continue;
            // 失败保留退出记录；只复制到当前父进程的合法可写用户页。
            if (status && uvm_copyout(parent->proc.pgtbl, status,
                                     (uint64)&child->status, sizeof(child->status)))
                return (uint64)-1;
            int pid = child->pid;
            assert(!child->proc.pgtbl && !child->proc.agent, "process: reap before cleanup");
            memset(child, 0, sizeof(*child));
            return pid;
        }
        if (!children) return (uint64)-1;
        suspend(parent, WAITING);
    }
}

void proc_exit(uint64 status)
{
    intr_off();
    process_slot_t *s = current();
    s->status = status;
#if AEGIS
    agent_record(&s->proc, AGENT_PROCESS_EXIT, 0, 0);
#endif
    suspend(s, ZOMBIE);
    panic("process: zombie resumed");
    __builtin_unreachable();
}

void proc_lifecycle_run(proc_t *initial)
{
    owner();
    assert(!myproc(), "process: scheduler already active");
    memset(slots, 0, sizeof(slots));
    next_pid = 1;
    slots[0].proc = *initial;
    memset(initial, 0, sizeof(*initial)); // 唯一资源所有权转移。
    slots[0].state = RUNNABLE;
    printf("aegis process lifecycle entering user mode\n");
    for (;;)
    {
        bool active = false, ran = false;
        for (int i = 0; i < PROC_SLOTS; i++)
        {
            process_slot_t *s = &slots[i];
            if (s->state == FREE) continue;
            active = true;
            if (s->state != RUNNABLE) continue;
            ran = true;
            s->state = RUNNING;
            mycpu()->proc = &s->proc;
            swtch(&mycpu()->ctx, &s->proc.ctx);
            owner();
            mycpu()->proc = NULL;
            if (s->state != ZOMBIE) continue;
            // 现已切回CPU启动栈；不能在退出者自己的栈上销毁资源。
            proc_release_resources(&s->proc);
            if (s->parent && s->parent->state == WAITING) s->parent->state = RUNNABLE;
            for (int j = 0; j < PROC_SLOTS; j++)
                if (slots[j].state != FREE && slots[j].parent == s)
                {
                    slots[j].parent = NULL;
                    if (slots[j].state == ZOMBIE) memset(&slots[j], 0, sizeof(slots[j]));
                }
            if (!s->parent) memset(s, 0, sizeof(*s));
        }
        if (!active) break;
        // 唯一等待源是直接子进程退出，不存在合法的全WAITING闭环。
        assert(ran, "process: lifecycle deadlock");
    }
    printf("aegis process lifecycle complete\n");
}
#endif
