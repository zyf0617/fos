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
#if AGENT_SCHED
    bool manager;
#endif
#if PROC_PREEMPT
    uint64 dispatches, timer_ticks, preemptions;
#endif
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

static uint64 fork_into(agent_group_t *group)
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
#if AGENT_SCHED
        int result = proc_clone_resources_to_group(&child->proc, &parent->proc, next_pid, group);
#else
        (void)group;
        int result = proc_clone_resources(&child->proc, &parent->proc, next_pid);
#endif
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
#if AGENT_SCHED
        child->manager = false;
#endif
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

uint64 proc_fork(void)
{
    return fork_into(current()->proc.agent);
}

#if AGENT_SCHED
uint64 proc_agentctl(uint64 command, uint64 value, uint64 argument)
{
    process_slot_t *s = current();
    if (command == AGENT_CTL_INFO)
    {
        agent_info_t info;
        agent_group_t *g = s->proc.agent;
        if (argument)
        {
            if (!s->manager && argument != g->id) return (uint64)-1;
            g = agent_managed_find(argument);
            if (!g) return (uint64)-1;
        }
        agent_get_info(g, &info);
        return uvm_copyout(s->proc.pgtbl, value, (uint64)&info, sizeof(info)) ? (uint64)-1 : 0;
    }
    if (!s->manager) return (uint64)-1;
    if (command == AGENT_CTL_CREATE)
    {
        agent_policy_t policy;
        agent_info_t source;
        if (uvm_copyin(s->proc.pgtbl, (uint64)&policy, value, sizeof(policy))) return (uint64)-1;
        agent_get_info(s->proc.agent, &source);
        return agent_managed_create(&policy, source.syscall_mask);
    }
    if (command == AGENT_CTL_REMOVE) return agent_managed_remove(value);
    agent_group_t *g = agent_managed_find(value);
    if (!g) return (uint64)-1;
    if (command == AGENT_CTL_WEIGHT) return agent_set_weight(g, argument);
    if (command == AGENT_CTL_SPAWN)
    {
        agent_info_t source, target;
        agent_get_info(s->proc.agent, &source);
        agent_get_info(g, &target);
        if (target.syscall_mask & ~source.syscall_mask) return (uint64)-1;
        return fork_into(g);
    }
    return (uint64)-1;
}
#endif

void proc_yield(void)
{
    suspend(current(), RUNNABLE);
}

#if PROC_PREEMPT
void proc_timer_tick(void)
{
    process_slot_t *s = current();
    s->timer_ticks++;
    for (int i = 0; i < PROC_SLOTS; i++)
        if (slots[i].state == RUNNABLE)
        {
            s->preemptions++;
            suspend(s, RUNNABLE);
            return;
        }
}
#endif

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

#if AGENT_SCHED
static uint64 group_floor, last_virtual_time;
static int group_cursor;

static process_slot_t *pick_next(void)
{
    agent_group_t *groups[AGENT_GROUP_CAPACITY];
    int count = agent_managed_list(groups);
    bool runnable[AGENT_GROUP_CAPACITY] = {0};
    uint64 existing = (uint64)-1;
    for (int n = 0; n < count; n++)
    {
        agent_group_t *g = groups[n];
        for (int i = 0; i < PROC_SLOTS; i++)
            if (slots[i].state == RUNNABLE && slots[i].proc.agent == g) runnable[n] = true;
        if (runnable[n] && g->ready && g->virtual_time < existing) existing = g->virtual_time;
    }
    // 先更新旧就绪组的时钟，再接纳新组；单组长时间运行后也不送出历史信用。
    if (existing == (uint64)-1) existing = last_virtual_time;
    if (existing > group_floor) group_floor = existing;
    uint64 minimum = (uint64)-1;
    for (int n = 0; n < count; n++)
    {
        agent_group_t *g = groups[n];
        if (runnable[n])
        {
            // 新组/重新就绪的组不能积攒休眠期间的调度信用。
            if (!g->ready && g->virtual_time < group_floor) g->virtual_time = group_floor;
            if (g->virtual_time < minimum) minimum = g->virtual_time;
        }
        g->ready = runnable[n];
    }
    if (minimum == (uint64)-1) return NULL;
    group_floor = minimum;
    for (int offset = 1; offset <= count; offset++)
    {
        int n = (group_cursor + offset) % count;
        agent_group_t *g = groups[n];
        if (!g->ready || g->virtual_time != minimum) continue;
        group_cursor = n;
        for (int k = 1; k <= PROC_SLOTS; k++)
        {
            int i = (g->member_cursor + k) % PROC_SLOTS;
            if (slots[i].state != RUNNABLE || slots[i].proc.agent != g) continue;
            g->member_cursor = i;
            return &slots[i];
        }
    }
    panic("group scheduler: missing member");
    __builtin_unreachable();
}

static void charge_runtime(agent_group_t *g, uint64 elapsed, uint64 weight)
{
    if (!elapsed) elapsed = 1;
    if (g->divisor != weight) { g->remainder = 0; g->divisor = weight; }
    // 保留除法余数，避免频繁yield通过取整抹掉收费。
    g->virtual_time += elapsed / weight;
    uint64 rest = elapsed % weight + g->remainder;
    g->virtual_time += rest / weight;
    g->remainder = rest % weight;
    last_virtual_time = g->virtual_time;
}
#else
static int process_cursor;
static process_slot_t *pick_next(void)
{
    for (int k = 1; k <= PROC_SLOTS; k++)
    {
        int i = (process_cursor + k) % PROC_SLOTS;
        if (slots[i].state != RUNNABLE) continue;
        process_cursor = i;
        return &slots[i];
    }
    return NULL;
}
#endif

void proc_lifecycle_run(proc_t *initial)
{
    owner();
    assert(!myproc(), "process: scheduler already active");
    memset(slots, 0, sizeof(slots));
    next_pid = 1;
#if AGENT_SCHED
    slots[0].manager = true;
    group_floor = last_virtual_time = 0;
    group_cursor = AGENT_GROUP_CAPACITY - 1;
#else
    process_cursor = PROC_SLOTS - 1;
#endif
    slots[0].proc = *initial;
    memset(initial, 0, sizeof(*initial));
    slots[0].state = RUNNABLE;
    printf("aegis process lifecycle entering user mode\n");
    for (;;)
    {
        bool active = false;
        for (int i = 0; i < PROC_SLOTS; i++)
            if (slots[i].state != FREE) active = true;
        if (!active) break;
        process_slot_t *s = pick_next();
        assert(s != NULL, "process: lifecycle deadlock");
#if PROC_PREEMPT
        s->dispatches++;
#endif
        s->state = RUNNING;
        mycpu()->proc = &s->proc;
#if AGENT_SCHED
        agent_group_t *g = s->proc.agent;
        uint64 weight = g->weight;
        uint64 started = r_time();
#endif
        swtch(&mycpu()->ctx, &s->proc.ctx);
        owner();
        mycpu()->proc = NULL;
#if AGENT_SCHED
        charge_runtime(g, r_time() - started, weight);
#endif
        if (s->state != ZOMBIE) continue;
        // 已回到CPU栈，才能销毁退出进程的地址空间及trapframe。
#if PROC_PREEMPT && !AGENT_SCHED
        printf("aegis rr pid=%d dispatches=%d ticks=%d preemptions=%d\n",
               s->pid, (int)s->dispatches, (int)s->timer_ticks, (int)s->preemptions);
#endif
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
    printf("aegis process lifecycle complete\n");
}
#endif
