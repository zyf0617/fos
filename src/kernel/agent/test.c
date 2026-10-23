#include "mod.h"
#include "../syscall/mod.h"
#ifdef AGENT_TEST

#if AEGIS_AUDIT
static agent_event_t captured[AGENT_AUDIT_CAPACITY];
#endif
static agent_group_t concurrent_group;
static proc_t concurrent_members[NCPU];
static int concurrent_start, concurrent_attempted, concurrent_success, concurrent_done, concurrent_claim;

static void concurrent_churn(void)
{
    proc_t *p = &concurrent_members[mycpuid()];
    mycpu()->proc = p;
    bool first = !agent_charge(p->pgtbl, 1);
    if (first) __atomic_fetch_add(&concurrent_success, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&concurrent_attempted, 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&concurrent_attempted, __ATOMIC_ACQUIRE) != NCPU - FIRST_HART) ;
    if (first) agent_uncharge(p->pgtbl, 1);
    for (int i = 0; i < 1000; i++)
        if (!agent_charge(p->pgtbl, 1))
        {
            assert(__sync_bool_compare_and_swap(&concurrent_claim, 0, 1),
                   "agent test: concurrent quota oversubscribed");
            __sync_lock_release(&concurrent_claim);
            agent_uncharge(p->pgtbl, 1);
        }
    mycpu()->proc = NULL;
}

void agent_test_worker(void)
{
    while (!__atomic_load_n(&concurrent_start, __ATOMIC_ACQUIRE)) ;
    concurrent_churn();
    __atomic_fetch_add(&concurrent_done, 1, __ATOMIC_RELEASE);
}

static void concurrent_test(void)
{
    agent_group_init(&concurrent_group, 44, 1, NCPU - FIRST_HART, AGENT_SYSCALL_MASK);
    for (int i = FIRST_HART; i < NCPU; i++)
    {
        concurrent_members[i].pgtbl = pmem_alloc(true);
        assert(concurrent_members[i].pgtbl && !agent_attach(&concurrent_members[i], &concurrent_group, 0),
               "agent test: concurrent setup");
    }
    __atomic_store_n(&concurrent_start, 1, __ATOMIC_RELEASE);
    concurrent_churn();
    while (__atomic_load_n(&concurrent_done, __ATOMIC_ACQUIRE) != NCPU - FIRST_HART - 1) ;
    agent_stats_t stats;
    agent_snapshot(&concurrent_group, &stats);
    assert(concurrent_success == 1 && stats.peak_pages == 1 && !stats.pages &&
           stats.quota_denied >= NCPU - FIRST_HART - 1,
           "agent test: concurrent reservation barrier");
    for (int i = FIRST_HART; i < NCPU; i++)
    {
        uvm_destroy_pgtbl(concurrent_members[i].pgtbl);
        agent_detach(&concurrent_members[i]);
    }
    printf("aegis concurrent budget PASS\n");
}

static uint64 hash_tree(pgtbl_t root, int level)
{
    uint64 value = 1469598103934665603ul;
    for (int i = 0; i < 512; i++)
    {
        value = (value ^ root[i]) * 1099511628211ul;
        if (level && (root[i] & PTE_V) && PTE_CHECK(root[i]))
            value ^= hash_tree((pgtbl_t)PTE_TO_PA(root[i]), level - 1);
    }
    return value;
}

void agent_self_test(void)
{
    uint32 kernel = pmem_available(true), user = pmem_available(false), nodes = mmap_available();
    agent_group_t group;
    agent_group_init(&group, 42, 4, 2, AGENT_SYSCALL_MASK & ~(1ul << SYS_copyout));
    proc_t p = {0}, peer = {0}, extra = {0};
    trapframe_t tf = {0};
    p.tf = &tf;
    p.pgtbl = pmem_alloc(true);
    assert(p.pgtbl && !agent_attach(&p, &group, 0) && !agent_attach(&peer, &group, 0),
           "agent test: attach");
    assert(agent_attach(&p, &group, 0) == -1 && agent_attach(&extra, &group, 0) == -1 &&
           group.processes == 2, "agent test: member limit");
    mycpu()->proc = &p;
    assert(!agent_charge(p.pgtbl, 3), "agent test: first member charge");
    peer.pgtbl = (pgtbl_t)1; // 只用于计数接口归属比较，不访问该地址。
    mycpu()->proc = &peer;
    assert(agent_charge(peer.pgtbl, 2) == -1 && group.pages == 3 && !peer.agent_pages,
           "agent test: shared budget bypass");
    assert(!agent_charge(peer.pgtbl, 1), "agent test: shared remaining page");
    agent_detach(&peer);
    agent_detach(&peer);
    mycpu()->proc = &p;
    agent_uncharge(p.pgtbl, 3);
    assert(!group.pages && group.processes == 1, "agent test: detach accounting");

    // 对每个物理分配失败点，预算、页表及两类空闲页必须保持原状。
    uint64 before = hash_tree(p.pgtbl, 2);
    uint32 k = pmem_available(true), u = pmem_available(false);
    bool completed = false;
    for (int n = 0; n < 16; n++)
    {
        pmem_test_fail_after(n);
        uint64 top = uvm_heap_grow(p.pgtbl, USER_HEAP_BASE, 3 * PGSIZE);
        pmem_test_fail_after(-1);
        if (top)
        {
            assert(group.pages == 3 && p.agent_pages == 3, "agent test: heap charge");
            assert(uvm_heap_ungrow(p.pgtbl, top, 3 * PGSIZE) == USER_HEAP_BASE,
                   "agent test: heap release");
            completed = true;
        }
        assert(!group.pages && !p.agent_pages && hash_tree(p.pgtbl, 2) == before &&
               pmem_available(true) == k && pmem_available(false) == u,
               "agent test: OOM budget or mapping leak");
        if (completed) break;
    }
    assert(completed, "agent test: no successful heap transaction");
    assert(uvm_heap_grow(p.pgtbl, USER_HEAP_BASE, 5 * PGSIZE) == 0 &&
           !group.pages && hash_tree(p.pgtbl, 2) == before,
           "agent test: heap quota");

    completed = false;
    uint64 mapping = 0;
    for (int n = 0; n < 16; n++)
    {
        pmem_test_fail_after(n);
        mapping = uvm_mmap(0, 2, PTE_R | PTE_W);
        pmem_test_fail_after(-1);
        if (mapping) { completed = true; break; }
        assert(!group.pages && !p.agent_pages && !p.mmap &&
               hash_tree(p.pgtbl, 2) == before && pmem_available(true) == k &&
               pmem_available(false) == u && mmap_available() == nodes,
               "agent test: mmap OOM leaked budget or node");
    }
    assert(completed, "agent test: no successful mmap transaction");
    assert(mapping && group.pages == 2 && uvm_mmap(0, 3, PTE_R) == 0 &&
           group.pages == 2 && p.mmap->npages == 2, "agent test: mmap quota");
    uint64 stack = (uint64)pmem_alloc(false);
    assert(stack && !vm_mappages(p.pgtbl, USER_STACK_TOP - PGSIZE, stack,
           PGSIZE, PTE_R | PTE_W | PTE_U) && !agent_charge(p.pgtbl, 1),
           "agent test: initial stack");
    before = hash_tree(p.pgtbl, 2);
    assert(uvm_ustack_grow(p.pgtbl, 1, USER_STACK_TOP - 3 * PGSIZE) == -1 &&
           group.pages == 3 && hash_tree(p.pgtbl, 2) == before,
           "agent test: stack quota");
    k = pmem_available(true); u = pmem_available(false);
    completed = false;
    for (int n = 0; n < 16; n++)
    {
        pmem_test_fail_after(n);
        int64 pages = uvm_ustack_grow(p.pgtbl, 1, USER_STACK_TOP - 2 * PGSIZE);
        pmem_test_fail_after(-1);
        if (pages == 2) { completed = true; break; }
        assert(group.pages == 3 && p.agent_pages == 3 && hash_tree(p.pgtbl, 2) == before &&
               pmem_available(true) == k && pmem_available(false) == u,
               "agent test: stack OOM leaked budget");
    }
    assert(completed && group.pages == 4, "agent test: stack charge");

    char *data = (char *)PTE_TO_PA(*vm_getpte(p.pgtbl, mapping, false));
    memset(data, 0x5a, 20);
    tf.a7 = SYS_copyout; tf.a0 = mapping;
    syscall();
    assert(tf.a0 == (uint64)-1, "agent test: capability enforcement");
    for (int i = 0; i < 20; i++) assert(data[i] == 0x5a, "agent test: denied service ran");
    tf.a7 = 0x100000002ul; tf.a0 = mapping;
    syscall();
    assert(tf.a0 == (uint64)-1, "agent test: truncated syscall number");
    assert(!uvm_munmap(mapping, 2) && group.pages == 2 && !p.mmap,
           "agent test: mmap release");

#if AEGIS_AUDIT
    for (int i = 0; i < AGENT_AUDIT_CAPACITY + 6; i++)
        agent_record(&p, AGENT_SYSCALL, i, 0);
    uint64 lost;
    uint32 count = agent_audit_read(0, captured, AGENT_AUDIT_CAPACITY, &lost);
    assert(count == AGENT_AUDIT_CAPACITY && lost >= 6 && captured[0].sequence == lost + 1,
           "agent test: audit overflow");
    for (uint32 i = 1; i < count; i++)
        assert(captured[i].sequence == captured[i - 1].sequence + 1 &&
               captured[i].group == 42 && captured[i].pages == 2,
               "agent test: audit ordering");
    assert(agent_audit_read(~0ul, captured, 1, &lost) == 0 && !lost &&
           agent_audit_read(0, NULL, 0, &lost) == 0 && lost,
           "agent test: audit boundaries");
#else
    uint64 lost = 99;
    assert(agent_audit_read(0, NULL, 0, &lost) == 0 && !lost,
           "agent test: disabled audit is not empty");
#endif
    agent_stats_t stats;
    agent_snapshot(&group, &stats);
    assert(stats.pages == 2 && stats.peak_pages == 4 && stats.processes == 1 &&
           stats.quota_denied >= 3 && stats.capability_denied == 1,
           "agent test: stats snapshot");
    assert(agent_restrict(&group, 1, 1, 0) == -1 &&
           agent_restrict(&group, 4, 0, 0) == -1 &&
           agent_restrict(&group, 4, 1, AGENT_SYSCALL_MASK) == -1,
           "agent test: invalid restriction accepted");
    assert(!agent_restrict(&group, 2, 1, 0) && !agent_allowed(&p, SYS_printf) &&
           agent_restrict(&group, 3, 1, 0) == -1 &&
           agent_restrict(&group, 2, 1, 1) == -1,
           "agent test: policy relaxed");
    uvm_destroy_pgtbl(p.pgtbl);
    p.pgtbl = NULL;
    agent_detach(&p);
    assert(!group.pages && !group.processes, "agent test: final group leak");

    // deny mmap必须保持该调用的0失败ABI；未入组时允许集合不生效。
    agent_group_init(&group, 43, 0, 1, 0);
    assert(!agent_attach(&p, &group, 0), "agent test: deny group");
    tf.a7 = SYS_mmap; tf.a0 = 0; tf.a1 = PGSIZE;
    syscall();
    assert(!tf.a0, "agent test: mmap denial ABI");
    agent_detach(&p);
    assert(agent_allowed(&p, SYS_copyout), "agent test: ordinary process restricted");
    mycpu()->proc = NULL;
    concurrent_test();
    assert(pmem_available(true) == kernel && pmem_available(false) == user &&
           mmap_available() == nodes && !mycpu()->noff,
           "agent test: final resource leak");
    printf("aegis kernel protection PASS\n");
}
#endif
