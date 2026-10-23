#include "mod.h"
#include "../../user/syscall_num.h"
#if AEGIS

static agent_group_t boot_group;
#if AEGIS_AUDIT
static spinlock_t audit_lock;
static agent_event_t events[AGENT_AUDIT_CAPACITY];
static uint64 sequence;
#endif

void agent_group_init(agent_group_t *g, uint64 id, uint64 pages,
                      uint64 processes, uint64 syscall_mask)
{
    assert(g && !(syscall_mask & ~AGENT_SYSCALL_MASK), "agent: invalid policy");
    memset(g, 0, sizeof(*g));
    spinlock_init(&g->lock, "agent.group");
    g->id = id;
    g->page_limit = pages;
    g->process_limit = processes;
    g->syscall_mask = syscall_mask;
}

void agent_init(void)
{
#if AEGIS_AUDIT
    spinlock_init(&audit_lock, "agent.audit");
    sequence = 0;
#endif
#ifdef AGENT_TEST
    agent_group_init(&boot_group, 1, 4, 1, AGENT_SYSCALL_MASK & ~(1ul << SYS_copyout));
#else
    agent_group_init(&boot_group, 1, 64, 1, AGENT_SYSCALL_MASK);
#endif
}

void agent_record(proc_t *p, uint64 kind, uint64 detail, uint64 result)
{
#if AEGIS_AUDIT
    if (!p || !p->agent)
        return;
    agent_group_t *g = p->agent;
    spinlock_acquire(&g->lock);
    uint64 pages = g->pages, id = g->id;
    spinlock_release(&g->lock);
    spinlock_acquire(&audit_lock);
    uint64 next = ++sequence;
    events[(next - 1) % AGENT_AUDIT_CAPACITY] =
        (agent_event_t){next, r_time(), id, (uint64)p->pid, kind, detail, result, pages};
    spinlock_release(&audit_lock);
#else
    (void)p; (void)kind; (void)detail; (void)result;
#endif
}

void agent_snapshot(agent_group_t *g, agent_stats_t *out)
{
    assert(g && out, "agent_snapshot: NULL argument");
    spinlock_acquire(&g->lock);
    *out = (agent_stats_t){g->id, g->page_limit, g->pages, g->peak_pages,
                          g->process_limit, g->processes, g->syscall_mask,
                          g->quota_denied, g->capability_denied};
    spinlock_release(&g->lock);
}

// 内核管理接口只允许收紧；不得在活动用量之下设置限额或增加权限。
int agent_restrict(agent_group_t *g, uint64 pages, uint64 processes, uint64 syscall_mask)
{
    spinlock_acquire(&g->lock);
    if (pages > g->page_limit || pages < g->pages || processes > g->process_limit ||
        processes < g->processes || (syscall_mask & ~g->syscall_mask))
    {
        spinlock_release(&g->lock);
        return -1;
    }
    g->page_limit = pages;
    g->process_limit = processes;
    g->syscall_mask = syscall_mask;
    spinlock_release(&g->lock);
    return 0;
}

int agent_attach(proc_t *p, agent_group_t *g, uint64 initial_pages)
{
    assert(p && g, "agent_attach: NULL owner");
    if (p->agent)
        return -1;
    spinlock_acquire(&g->lock);
    if (g->processes >= g->process_limit || initial_pages > g->page_limit - g->pages)
    {
        spinlock_release(&g->lock);
        return -1;
    }
    g->processes++;
    g->pages += initial_pages;
    if (g->pages > g->peak_pages) g->peak_pages = g->pages;
    p->agent_pages = initial_pages;
    p->agent = g;
    spinlock_release(&g->lock);
    agent_record(p, AGENT_ATTACH, initial_pages, 0);
    return 0;
}

int agent_attach_boot(proc_t *p)
{
    return agent_attach(p, &boot_group, 2);
}

void agent_boot_check(void)
{
    assert(!boot_group.pages && !boot_group.processes, "agent: boot group leaked");
    printf("aegis group cleanup PASS\n");
}

void agent_detach(proc_t *p)
{
    if (!p->agent)
        return;
    agent_group_t *g = p->agent;
    spinlock_acquire(&g->lock);
    assert(g->processes && g->pages >= p->agent_pages, "agent_detach: accounting");
    g->pages -= p->agent_pages;
    g->processes--;
    p->agent_pages = 0;
    spinlock_release(&g->lock);
    agent_record(p, AGENT_DETACH, 0, 0);
    p->agent = NULL;
}

int agent_charge(pgtbl_t root, uint64 pages)
{
    proc_t *p = myproc();
    if (!p || p->pgtbl != root || !p->agent || !pages)
        return 0;
    agent_group_t *g = p->agent;
    spinlock_acquire(&g->lock);
    if (pages > g->page_limit - g->pages)
    {
        g->quota_denied++;
        spinlock_release(&g->lock);
        agent_record(p, AGENT_QUOTA_DENY, pages, (uint64)-1);
        return -1;
    }
    g->pages += pages;
    if (g->pages > g->peak_pages) g->peak_pages = g->pages;
    p->agent_pages += pages;
    spinlock_release(&g->lock);
    return 0;
}

void agent_uncharge(pgtbl_t root, uint64 pages)
{
    proc_t *p = myproc();
    if (!p || p->pgtbl != root || !p->agent || !pages)
        return;
    agent_group_t *g = p->agent;
    spinlock_acquire(&g->lock);
    assert(pages <= p->agent_pages && pages <= g->pages, "agent_uncharge: accounting");
    p->agent_pages -= pages;
    g->pages -= pages;
    spinlock_release(&g->lock);
}

bool agent_allowed(proc_t *p, uint64 number)
{
    if (!p->agent)
        return true;
    agent_group_t *g = p->agent;
    spinlock_acquire(&g->lock);
    bool allowed = number < 8 && (g->syscall_mask & (1ul << number));
    if (!allowed) g->capability_denied++;
    spinlock_release(&g->lock);
    return allowed;
}

uint32 agent_audit_read(uint64 after, agent_event_t *out, uint32 capacity, uint64 *lost)
{
    assert(lost && (out || !capacity), "agent_audit_read: invalid buffer");
#if AEGIS_AUDIT
    spinlock_acquire(&audit_lock);
    *lost = 0;
    uint32 count = 0;
    if (after < sequence)
    {
        uint64 oldest = sequence > AGENT_AUDIT_CAPACITY ? sequence - AGENT_AUDIT_CAPACITY + 1 : 1;
        uint64 first = after + 1;
        if (first < oldest) { *lost = oldest - first; first = oldest; }
        for (uint64 i = first; i <= sequence && count < capacity; i++)
            out[count++] = events[(i - 1) % AGENT_AUDIT_CAPACITY];
    }
    spinlock_release(&audit_lock);
    return count;
#else
    (void)after;
    *lost = 0;
    return 0;
#endif
}

void agent_audit_dump(void)
{
    // 每次只在锁内复制一条；串口输出完全在锁外，避免扩大热路径临界区。
    uint64 cursor = 0, lost;
    agent_event_t event;
    while (agent_audit_read(cursor, &event, 1, &lost))
    {
        if (lost) printf("aegis audit lost=%d\n", (int)lost);
        printf("aegis audit seq=%d group=%d pid=%d kind=%d detail=%p result=%p pages=%d\n",
               (int)event.sequence, (int)event.group, (int)event.pid, (int)event.kind,
               event.detail, event.result, (int)event.pages);
        cursor = event.sequence;
    }
}
#endif
