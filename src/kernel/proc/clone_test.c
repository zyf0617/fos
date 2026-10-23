#include "mod.h"
#include "../agent/mod.h"
#ifdef LAB5_TEST

static uint64 source_digest(pgtbl_t root, int level)
{
    uint64 hash = 1469598103934665603ul;
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = root[i];
        hash = (hash ^ pte) * 1099511628211ul;
        if (!(pte & PTE_V)) continue;
        if (PTE_CHECK(pte))
            hash ^= source_digest((pgtbl_t)PTE_TO_PA(pte), level - 1);
        else if (pte & PTE_U)
        {
            assert(!level, "clone test: unexpected huge page");
            uint8 *data = (uint8 *)PTE_TO_PA(pte);
            for (int j = 0; j < PGSIZE; j++) hash = (hash ^ data[j]) * 1099511628211ul;
        }
    }
    return hash;
}

// 完整比较三级树中所有用户叶页：权限、内容、物理所有权均须独立。
static uint64 check_tree(pgtbl_t src, pgtbl_t dst, int level, uint64 prefix)
{
    uint64 pages = 0;
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = src[i];
        if (!(pte & PTE_V)) continue;
        uint64 va = prefix | ((uint64)i << VA_SHIFT(level));
        if (PTE_CHECK(pte))
            pages += check_tree((pgtbl_t)PTE_TO_PA(pte), dst, level - 1, va);
        else if (pte & PTE_U)
        {
            pte_t *copy = vm_getpte(dst, va, false);
            assert(!level && copy && PTE_FLAGS(*copy) == PTE_FLAGS(pte) &&
                   PTE_TO_PA(*copy) != PTE_TO_PA(pte), "clone test: leaf ownership/permissions");
            uint8 *a = (uint8 *)PTE_TO_PA(pte), *b = (uint8 *)PTE_TO_PA(*copy);
            for (int j = 0; j < PGSIZE; j++)
                assert(a[j] == b[j], "clone test: page contents");
            uint8 saved = a[0];
            b[0] ^= 0xff;
            assert(a[0] == saved, "clone test: child aliases parent");
            b[0] = saved;
            a[1] ^= 0xff;
            assert(b[1] != a[1], "clone test: parent aliases child");
            a[1] ^= 0xff;
            pages++;
        }
    }
    return pages;
}

static void check_empty(proc_t *p)
{
    for (uint64 i = 0; i < sizeof(*p); i++)
        assert(!((uint8 *)p)[i], "clone test: partial child published");
}

void proc_clone_self_test(void)
{
    assert(!myproc(), "clone test: current process");
    uint32 initial_k = pmem_available(true), initial_u = pmem_available(false);
    uint32 initial_nodes = mmap_available();
    proc_t src = {0}, dst = {0};
    src.pid = 71;
    src.tf = pmem_alloc(true);
    assert(src.tf != NULL, "clone test: source trapframe");
    src.pgtbl = proc_pgtbl_init((uint64)src.tf);
    uint64 code = (uint64)pmem_alloc(false), stack = (uint64)pmem_alloc(false);
    assert(src.pgtbl && code && stack &&
           !vm_mappages(src.pgtbl, USER_BASE, code, PGSIZE, PTE_R | PTE_X | PTE_U) &&
           !vm_mappages(src.pgtbl, USER_STACK_TOP - PGSIZE, stack, PGSIZE, PTE_R | PTE_W | PTE_U),
           "clone test: source initial pages");
    src.heap_top = USER_HEAP_BASE;
    src.ustack_npage = 1;
    mycpu()->proc = &src;
    src.heap_top = uvm_heap_grow(src.pgtbl, src.heap_top, PGSIZE + 17);
    int64 grown = uvm_ustack_grow(src.pgtbl, 1, USER_STACK_TOP - 2 * PGSIZE);
    uint64 map = uvm_mmap(0, 2, PTE_R | PTE_W);
    uint64 ro = uvm_mmap(0, 1, PTE_R);
    assert(src.heap_top == USER_HEAP_BASE + PGSIZE + 17 && grown == 2 && map && ro &&
           src.mmap && src.mmap->next && !src.mmap->next->next, "clone test: source layout");
    src.ustack_npage = grown;
    mycpu()->proc = NULL;
    // 1 code + 2 heap + 2 stack + 3 mmap = 8页。
    memset((void *)code, 0xa6, PGSIZE);
    memset((void *)PTE_TO_PA(*vm_getpte(src.pgtbl, map, false)), 0x5a, PGSIZE);
    src.tf->a0 = 123;
    src.tf->s11 = 456;
    src.tf->sp = USER_STACK_TOP - 32;
    src.tf->user_to_kern_epc = USER_BASE + 20;
    src.tf->user_to_kern_satp = 1;
    src.tf->user_to_kern_sp = 2;
    src.tf->user_to_kern_trapvector = 3;
    src.tf->user_to_kern_hartid = 4;
    uint32 k = pmem_available(true), u = pmem_available(false), nodes = mmap_available();
    assert(!proc_clone_resources(&dst, &src, 72) && !dst.agent && !dst.agent_pages &&
           check_tree(src.pgtbl, dst.pgtbl, 2, 0) == 8, "clone test: ordinary copy");
    proc_release_resources(&dst);
#if AEGIS
    agent_group_t group;
    agent_group_init(&group, 74, 16, 2, AGENT_SYSCALL_MASK);
    assert(!agent_attach(&src, &group, 8), "clone test: source attach");
#endif
    uint64 digest = source_digest(src.pgtbl, 2);
    bool completed = false;
    int failures = 0;
    for (int n = 0; n < 64; n++)
    {
        pmem_test_fail_after(n);
        int result = proc_clone_resources(&dst, &src, 72);
        pmem_test_fail_after(-1);
        if (!result)
        {
            completed = true;
            assert(check_tree(src.pgtbl, dst.pgtbl, 2, 0) == 8 && dst.tf != src.tf &&
                   dst.tf->a0 == 0 && src.tf->a0 == 123 && dst.tf->s11 == 456 &&
                   dst.tf->sp == src.tf->sp && dst.tf->user_to_kern_epc == src.tf->user_to_kern_epc &&
                   !dst.tf->user_to_kern_satp && !dst.tf->user_to_kern_sp &&
                   !dst.tf->user_to_kern_trapvector && !dst.tf->user_to_kern_hartid &&
                   !dst.kstack && !dst.ctx.sp && !dst.ctx.ra,
                   "clone test: register/context ownership");
            mmap_region_t *a = src.mmap, *b = dst.mmap;
            for (; a && b; a = a->next, b = b->next)
                assert(a != b && a->begin == b->begin && a->npages == b->npages &&
                       a->perm == b->perm, "clone test: mmap metadata");
            assert(!a && !b && dst.heap_top == src.heap_top && dst.ustack_npage == 2 &&
                   PTE_TO_PA(*vm_getpte(dst.pgtbl, TRAPFRAME, false)) == (uint64)dst.tf &&
                   PTE_TO_PA(*vm_getpte(dst.pgtbl, TRAMPOLINE, false)) ==
                   PTE_TO_PA(*vm_getpte(src.pgtbl, TRAMPOLINE, false)), "clone test: special mappings");
#if AEGIS
            assert(dst.agent == src.agent && dst.agent_pages == 8 && group.pages == 16 &&
                   group.processes == 2, "clone test: inherited group budget");
            assert(!agent_restrict(&group, 16, 2, 0) && !agent_allowed(&src, 7) &&
                   !agent_allowed(&dst, 7), "clone test: inherited restriction");
#endif
            proc_release_resources(&dst);
        }
        else failures++;
        check_empty(&dst);
        assert(source_digest(src.pgtbl, 2) == digest && src.tf->a0 == 123 &&
               src.heap_top == USER_HEAP_BASE + PGSIZE + 17 && src.ustack_npage == 2 &&
               src.mmap->begin == map && src.mmap->next->begin == ro,
               "clone test: source changed");
        assert(!myproc() && !mycpu()->noff && pmem_available(true) == k &&
               pmem_available(false) == u && mmap_available() == nodes,
               "clone test: allocation rollback leak");
#if AEGIS
        assert(group.pages == 8 && group.processes == 1 && src.agent_pages == 8,
               "clone test: reservation rollback leak");
#endif
        if (completed) break;
    }
    assert(completed && failures > 8, "clone test: incomplete failure sweep");
    // 留一个空闲节点：第二个区间复制失败时必须退还第一个节点及组预留。
    static mmap_region_t *held[N_MMAP];
    uint32 count = 0;
    while (mmap_available() > 1) held[count++] = mmap_region_alloc();
    assert(proc_clone_resources(&dst, &src, 72) == -1 && mmap_available() == 1,
           "clone test: partial node rollback");
    check_empty(&dst);
    for (uint32 i = 0; i < count; i++) mmap_region_free(held[i]);
#if AEGIS
    assert(!agent_restrict(&group, 15, 2, 0) && proc_clone_resources(&dst, &src, 72) == -1 &&
           !agent_restrict(&group, 15, 1, 0) && proc_clone_resources(&dst, &src, 72) == -1 &&
           group.pages == 8 && group.processes == 1, "clone test: quota/member rejection");
    check_empty(&dst);
#endif
    assert(pmem_available(true) == k && pmem_available(false) == u && mmap_available() == nodes,
           "clone test: failure leaked resources");
    proc_release_resources(&dst); // 重复清理保持无操作。
    proc_release_resources(&src);
#if AEGIS
    assert(!group.pages && !group.processes, "clone test: source cleanup accounting");
#endif
    assert(pmem_available(true) == initial_k && pmem_available(false) == initial_u &&
           mmap_available() == initial_nodes && !mycpu()->noff, "clone test: final cleanup");
    printf("aegis process resource clone PASS failures=%d\n", failures);
}
#endif
