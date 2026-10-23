#include "mod.h"
#include "../syscall/mod.h"
#ifdef AGENT_TEST

static agent_event_t captured[AGENT_AUDIT_CAPACITY];

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
    assert(pmem_available(true) == kernel && pmem_available(false) == user &&
           mmap_available() == nodes && !mycpu()->noff,
           "agent test: final resource leak");
    printf("aegis kernel protection PASS\n");
}
#endif
