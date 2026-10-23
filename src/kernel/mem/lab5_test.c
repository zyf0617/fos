#include "mod.h"
#ifdef LAB5_TEST
extern char trampoline[];

static uint64 tree_hash(pgtbl_t root, int level)
{
    uint64 hash = 1469598103934665603ul;
    for (int i = 0; i < 512; i++)
    {
        hash = (hash ^ root[i]) * 1099511628211ul;
        if ((root[i] & PTE_V) && PTE_CHECK(root[i]) && level)
            hash ^= tree_hash((pgtbl_t)PTE_TO_PA(root[i]), level - 1);
    }
    return hash;
}

typedef struct snapshot { uint32 kernel, user, nodes; uint64 hash, metadata; } snapshot_t;
static snapshot_t snapshot(pgtbl_t root)
{
    uint64 metadata = 0;
    proc_t *p = myproc();
    if (p && p->pgtbl == root)
    {
        metadata = p->heap_top ^ p->ustack_npage ^ (uint64)p->mmap;
        for (mmap_region_t *r = p->mmap; r; r = r->next)
            metadata = metadata * 1099511628211ul ^ r->begin ^ r->npages ^ r->perm ^ (uint64)r->next;
    }
    return (snapshot_t){pmem_available(true), pmem_available(false), mmap_available(), tree_hash(root, 2), metadata};
}
static void unchanged(pgtbl_t root, snapshot_t old)
{
    snapshot_t now = snapshot(root);
    assert(now.kernel == old.kernel && now.user == old.user && now.nodes == old.nodes && now.hash == old.hash && now.metadata == old.metadata,
           "lab5 test: transaction changed original tree or leaked");
}
static uint64 mapped_pa(pgtbl_t root, uint64 va)
{
    pte_t *pte = vm_getpte(root, va, false);
    assert(pte && (*pte & PTE_V), "lab5 test: missing mapping");
    return PTE_TO_PA(*pte);
}
static pgtbl_t make_test_root(uint64 *trapframe)
{
    *trapframe = (uint64)pmem_alloc(true);
    assert(*trapframe != 0, "lab5 test: trapframe alloc");
    pgtbl_t root = proc_pgtbl_init(*trapframe);
    assert(root != NULL, "lab5 test: root alloc");
    return root;
}
static void copy_test(void)
{
    uint64 tf;
    pgtbl_t root = make_test_root(&tf);
    assert(uvm_heap_grow(root, USER_HEAP_BASE, 2 * PGSIZE) == USER_HEAP_BASE + 2 * PGSIZE,
           "lab5 copy: allocate");
    char input[] = "cross-page", output[sizeof(input)];
    uint64 at = USER_HEAP_BASE + PGSIZE - 3;
    assert(!uvm_copyout(root, at, (uint64)input, sizeof(input)) &&
           !uvm_copyin(root, (uint64)output, at, sizeof(output)) &&
           !strncmp(input, output, sizeof(input)), "lab5 copy: cross page");
    assert(!uvm_copyin_str(root, (uint64)output, at, sizeof(output)) &&
           uvm_copyin_str(root, (uint64)output, at, sizeof(output) - 1) == -1,
           "lab5 copy: bounded NUL");
    pte_t *second = vm_getpte(root, USER_HEAP_BASE + PGSIZE, false);
    pte_t saved = *second;
    *second &= ~PTE_W;
    assert(uvm_copyout(root, at, (uint64)input, sizeof(input)) == -1, "lab5 copy: readonly write");
    *second = saved & ~PTE_U;
    assert(uvm_copyin(root, (uint64)output, at, sizeof(output)) == -1, "lab5 copy: supervisor page");
    *second = (saved & ~(PTE_R | PTE_W)) | PTE_X;
    assert(uvm_copyin(root, (uint64)output, at, sizeof(output)) == -1, "lab5 copy: execute only");
    *second = saved;
    assert(uvm_copyin(root, (uint64)output, TRAPFRAME, 1) == -1 &&
           uvm_copyin(root, (uint64)output, TRAMPOLINE, 1) == -1 &&
           uvm_copyin(root, (uint64)output, ~0ul, 1) == -1 &&
           uvm_copyin(root, (uint64)output, VA_MAX - 1, 2) == -1 &&
           !uvm_copyin(root, 0, ~0ul, 0) && !uvm_copyout(root, ~0ul, 0, 0) &&
           uvm_copyin_str(root, (uint64)output, at, 0) == -1, "lab5 copy: boundaries");
    // NUL位于已映射页最后一个字节，下一页没有映射也必须成功。
    char nul = 0;
    assert(!uvm_copyout(root, USER_HEAP_BASE + 2 * PGSIZE - 1, (uint64)&nul, 1) &&
           !uvm_copyin_str(root, (uint64)output, USER_HEAP_BASE + 2 * PGSIZE - 1, sizeof(output)),
           "lab5 copy: read beyond NUL");
    // 后一页失败时，前缀必须已正确复制。
    char pair[2] = {'q', 'r'};
    assert(!uvm_copyout(root, USER_HEAP_BASE + 2 * PGSIZE - 1, (uint64)pair, 1), "lab5 copy: prefix setup");
    output[0] = output[1] = 0;
    assert(uvm_copyin(root, (uint64)output, USER_HEAP_BASE + 2 * PGSIZE - 1, 2) == -1 &&
           output[0] == 'q' && output[1] == 0, "lab5 copy: partial prefix");
    uvm_destroy_pgtbl(root);
    pmem_free(tf, true);
    printf("lab-5 kernel copy PASS\n");
}

static void heap_stack_test(void)
{
    uint64 tf;
    pgtbl_t root = make_test_root(&tf);
    snapshot_t before = snapshot(root);
    assert(uvm_heap_grow(root, USER_HEAP_BASE, 0) == USER_HEAP_BASE &&
           uvm_heap_grow(root, MMAP_BEGIN, 1) == 0 &&
           uvm_heap_ungrow(root, USER_HEAP_BASE, 1) == 0, "lab5 heap: bounds");
    unchanged(root, before);
    // 跨2MiB边界，强制创建多个最低层页表。每个失败点都校验原树逐项相同。
    const uint64 top = (2ul << 20) - PGSIZE;
    bool success = false;
    for (int n = 0; n < 16; n++)
    {
        pmem_test_fail_after(n);
        uint64 result = uvm_heap_grow(root, top, 3 * PGSIZE);
        pmem_test_fail_after(-1);
        if (result)
        {
            success = true;
            assert(uvm_heap_ungrow(root, result, 3 * PGSIZE) == top, "lab5 heap: shrink");
            unchanged(root, before);
            break;
        }
        unchanged(root, before);
    }
    assert(success, "lab5 heap: no successful transaction");
    // 跨1GiB边界同时产生两级新页表；缩回后应立即归还，不等进程退出。
    const uint64 distant = (1ul << 30) - PGSIZE;
    assert(uvm_heap_grow(root, distant, 3 * PGSIZE) == distant + 3 * PGSIZE &&
           uvm_heap_ungrow(root, distant + 3 * PGSIZE, 3 * PGSIZE) == distant,
           "lab5 heap: cross root boundary");
    unchanged(root, before);
    assert(uvm_heap_grow(root, USER_HEAP_BASE, 1) == USER_HEAP_BASE + 1, "lab5 heap: subpage grow");
    uint32 user = pmem_available(false);
    assert(uvm_heap_grow(root, USER_HEAP_BASE + 1, 7) == USER_HEAP_BASE + 8 &&
           pmem_available(false) == user &&
           uvm_heap_ungrow(root, USER_HEAP_BASE + 8, 7) == USER_HEAP_BASE + 1 &&
           pmem_available(false) == user, "lab5 heap: byte top");
    // 独立初始栈页，故障地址一次跨四页，不能只补最后一页。
    uint64 page = (uint64)pmem_alloc(false);
    assert(page && !vm_mappages(root, USER_STACK_TOP - PGSIZE, page, PGSIZE, PTE_R | PTE_W | PTE_U),
           "lab5 stack: initial page");
    before = snapshot(root);success = false;
    for (int n = 0; n < 16; n++)
    {
        pmem_test_fail_after(n);
        int64 count = uvm_ustack_grow(root, 1, USER_STACK_TOP - 5 * PGSIZE + 7);
        pmem_test_fail_after(-1);
        if (count > 0) { assert(count == 5, "lab5 stack: count"); success = true; break; }
        unchanged(root, before);
    }
    assert(success && uvm_ustack_grow(root, 5, USER_STACK_TOP - PGSIZE) == -1 &&
           uvm_ustack_grow(root, 5, MMAP_END - 1) == -1 &&
           mapped_pa(root, USER_STACK_TOP - 4 * PGSIZE), "lab5 stack: range");
    uvm_destroy_pgtbl(root);pmem_free(tf, true);
    printf("lab-5 kernel heap-stack PASS\n");
}

static mmap_region_t *held[N_MMAP];
static volatile int pool_start, pool_done;
static int claims[N_MMAP];
static void pool_churn(void)
{
    for (int n = 0; n < 1000; n++)
    {
        mmap_region_t *node = mmap_region_alloc();
        assert(node != NULL, "lab5 pool: concurrent exhaustion");
        int i = 0;
        while (i < N_MMAP && held[i] != node) i++;
        assert(i < N_MMAP && __sync_bool_compare_and_swap(&claims[i], 0, 1), "lab5 pool: duplicate owner");
        __sync_lock_release(&claims[i]);
        mmap_region_free(node);
    }
}
void lab5_pool_worker(void)
{
    if (LAB4_SCENARIO != 14) return;
    while (!__atomic_load_n(&pool_start, __ATOMIC_ACQUIRE)) ;
    pool_churn();
    __atomic_fetch_add(&pool_done, 1, __ATOMIC_RELEASE);
}
static void pool_test(void)
{
    for (int i = 0; i < N_MMAP; i++) { held[i] = mmap_region_alloc(); assert(held[i] != NULL, "lab5 pool: early empty"); }
    assert(mmap_region_alloc() == NULL && mmap_available() == 0, "lab5 pool: capacity");
    for (int i = 0; i < N_MMAP; i++) mmap_region_free(held[i]);
    __atomic_store_n(&pool_start, 1, __ATOMIC_RELEASE);
    pool_churn();
    while (__atomic_load_n(&pool_done, __ATOMIC_ACQUIRE) != NCPU - FIRST_HART - 1) ;
    assert(mmap_available() == N_MMAP, "lab5 pool: concurrent leak");
    printf("lab-5 kernel pool concurrent PASS\n");
}
static void mmap_test(void)
{
    uint64 tf;
    proc_t process = {0};
    process.pgtbl = make_test_root(&tf);
    mycpu()->proc = &process;
    snapshot_t before = snapshot(process.pgtbl);
    snapshot_t empty = before;
    bool success = false;
    for (int n = 0; n < 16; n++)
    {
        pmem_test_fail_after(n);
        uint64 result = uvm_mmap(MMAP_BEGIN, 3, PTE_R | PTE_W);
        pmem_test_fail_after(-1);
        if (result) { success = true; break; }
        unchanged(process.pgtbl, before);
        assert(process.mmap == NULL, "lab5 mmap: partial publication");
    }
    assert(success, "lab5 mmap: no successful transaction");
    assert(!uvm_mmap(MMAP_BEGIN, 1, PTE_R) &&
           !uvm_mmap(MMAP_END, 1, PTE_R) && !uvm_mmap(MMAP_BEGIN + 1, 1, PTE_R), "lab5 mmap: invalid accepted");
    before = snapshot(process.pgtbl);
    // 耗尽节点，验证中段拆分失败不触及原映射。
    int count = 0;
    while (count < N_MMAP && (held[count] = mmap_region_alloc())) count++;
    assert(uvm_munmap(MMAP_BEGIN + PGSIZE, 1) == -1, "lab5 munmap: split exhaustion");
    for (int i = 0; i < count; i++) mmap_region_free(held[i]);
    unchanged(process.pgtbl, before);
    assert(uvm_munmap(MMAP_BEGIN + PGSIZE, 1) == 0 && process.mmap && process.mmap->next,
           "lab5 munmap: middle split");
    before = snapshot(process.pgtbl);
    assert(uvm_munmap(MMAP_BEGIN, 3) == -1, "lab5 munmap: hole accepted");
    unchanged(process.pgtbl, before);
    assert(uvm_mmap(MMAP_BEGIN + PGSIZE, 1, PTE_R | PTE_W) == MMAP_BEGIN + PGSIZE &&
           process.mmap->npages == 3 && !process.mmap->next, "lab5 mmap: bridge merge");
    assert(uvm_mmap(MMAP_BEGIN + 3 * PGSIZE, 2, PTE_R) == MMAP_BEGIN + 3 * PGSIZE &&
           process.mmap->next, "lab5 mmap: different perms merged");
    assert(!uvm_munmap(MMAP_BEGIN + PGSIZE, 3) && process.mmap->npages == 1 &&
           process.mmap->next->begin == MMAP_BEGIN + 4 * PGSIZE && process.mmap->next->npages == 1,
           "lab5 munmap: cross permission trim");
    assert(uvm_mmap(0, 1, PTE_R | PTE_W) == MMAP_BEGIN + PGSIZE, "lab5 mmap: first fit");
    assert(!uvm_munmap(MMAP_BEGIN, 2) && !uvm_munmap(MMAP_BEGIN + 4 * PGSIZE, 1) && !process.mmap,
           "lab5 munmap: full cleanup");
    unchanged(process.pgtbl, empty);
    uvm_destroy_pgtbl(process.pgtbl);pmem_free(tf, true);mycpu()->proc = NULL;
    printf("lab-5 kernel mmap PASS\n");
}

static void pagetable_test(void)
{
    uint64 tf_old, tf_new;
    pgtbl_t old = make_test_root(&tf_old), new = make_test_root(&tf_new);
    assert(uvm_heap_grow(old, USER_HEAP_BASE, 2 * PGSIZE), "lab5 clone: heap alloc");
    uint64 code = (uint64)pmem_alloc(false), stack = (uint64)pmem_alloc(false), map = (uint64)pmem_alloc(false);
    assert(code && stack && map &&
           !vm_mappages(old, USER_BASE, code, PGSIZE, PTE_R | PTE_X | PTE_U) &&
           !vm_mappages(old, USER_STACK_TOP - PGSIZE, stack, PGSIZE, PTE_R | PTE_W | PTE_U) &&
           !vm_mappages(old, MMAP_BEGIN, map, PGSIZE, PTE_R | PTE_U), "lab5 clone: mappings");
    memset((void *)code, 0x31, PGSIZE);memset((void *)stack, 0x32, PGSIZE);memset((void *)map, 0x33, PGSIZE);
    mmap_region_t region = {MMAP_BEGIN, 1, PTE_R | PTE_U, NULL};
    snapshot_t before = snapshot(new);
    uint64 source_hash = tree_hash(old, 2);
    bool success = false;
    for (int n = 0; n < 32; n++)
    {
        pmem_test_fail_after(n);
        int result = uvm_copy_pgtbl(old, new, USER_HEAP_BASE + 2 * PGSIZE, 1, &region);
        pmem_test_fail_after(-1);
        assert(tree_hash(old, 2) == source_hash, "lab5 clone: source changed");
        if (!result) { success = true; break; }
        unchanged(new, before);
    }
    assert(success, "lab5 clone: no successful transaction");
    const uint64 addresses[] = {USER_BASE, USER_HEAP_BASE, USER_HEAP_BASE + PGSIZE, USER_STACK_TOP - PGSIZE, MMAP_BEGIN};
    for (uint32 i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++)
    {
        uint64 va = addresses[i], a = mapped_pa(old, va), b = mapped_pa(new, va);
        pte_t original = *vm_getpte(old, va, false), cloned = *vm_getpte(new, va, false);
        assert(a != b && (original & 31) == (cloned & 31), "lab5 clone: alias or permission");
        for (uint32 j = 0; j < PGSIZE; j++)
            assert(((uint8 *)a)[j] == ((uint8 *)b)[j], "lab5 clone: content");
        ((uint8 *)b)[0] ^= 0xff;
        assert(((uint8 *)b)[0] != ((uint8 *)a)[0], "lab5 clone: not independent");
    }
    assert(mapped_pa(new, TRAPFRAME) == tf_new && mapped_pa(new, TRAMPOLINE) == (uint64)trampoline,
           "lab5 clone: special ownership");
    uvm_destroy_pgtbl(old);uvm_destroy_pgtbl(new);
    // 销毁后特殊物理页仍由调用者拥有且可读写。
    *(uint64 *)tf_old = 7;*(uint64 *)tf_new = 8;
    pmem_free(tf_old, true);pmem_free(tf_new, true);
    printf("lab-5 kernel pagetable PASS\n");
}

void lab5_self_test(void)
{
    uint32 kernel = pmem_available(true), user = pmem_available(false), nodes = mmap_available();
    int group = LAB4_SCENARIO;
    if (group == 10 || group == 14) copy_test();
    if (group == 11 || group == 14) heap_stack_test();
    if (group == 12 || group == 14) mmap_test();
    if (group == 13 || group == 14) pagetable_test();
    if (group == 14) pool_test();
    assert(pmem_available(true) == kernel && pmem_available(false) == user &&
           mmap_available() == nodes && myproc() == NULL && mycpu()->noff == 0,
           "lab5 test: final resource leak");
    printf("lab-5 kernel suite PASS\n");
}
#endif
