#include "mod.h"
#ifdef LAB4_TEST
static void *pages[KERN_PAGES];
void lab4_memory_test(void)
{
    // ELF只读段必须由硬件页表约束，不能仅依赖C语言const。
    pgtbl_t kernel = (pgtbl_t)((r_satp() & ((1ul << 44) - 1)) << 12);
    pte_t *readonly = vm_getpte(kernel, (uint64)"kernel readonly test", false);
    pte_t *writable = vm_getpte(kernel, (uint64)pages, false);
    pte_t *guard = vm_getpte(kernel, TRAPFRAME, false);
    assert(readonly && (*readonly & PTE_R) && !(*readonly & (PTE_W | PTE_U)),
           "memory test: readonly section writable");
    assert(writable && (*writable & PTE_W) && !(*writable & (PTE_X | PTE_U)),
           "memory test: kernel data permissions");
    assert(guard == NULL || !(*guard & PTE_V), "memory test: kernel stack guard mapped");
    uint32 before = pmem_available(true);
    for (uint32 i = 0; i < before; i++)
    {
        pages[i] = pmem_alloc(true);
        assert(pages[i] != NULL, "memory test: premature exhaustion");
    }
    assert(pmem_alloc(true) == NULL && pmem_available(true) == 0,
           "memory test: exhaustion must return NULL");
    for (uint32 i = 0; i < before; i++)
        pmem_free((uint64)pages[i], true);
    assert(pmem_available(true) == before, "memory test: exhaustion leak");

    pgtbl_t root = pmem_alloc(true);
    uint64 page = (uint64)pmem_alloc(false);
    assert(vm_mappages(root, USER_BASE, page, PGSIZE, PTE_R | PTE_U) == 0,
           "memory test: initial mapping failed");
    pte_t old = *vm_getpte(root, USER_BASE, false);
    uint32 baseline = pmem_available(true);
    // 跨最低层页表边界，第二个映射失败时第一叶项也必须撤销。
    for (int count = 0; count < 3; count++)
    {
        pmem_test_fail_after(count);
        int result = vm_mappages(root, (1ul << 30) - PGSIZE, page,
                                 2 * PGSIZE, PTE_R);
        pmem_test_fail_after(-1);
        assert(result == -1 && pmem_available(true) == baseline &&
               *vm_getpte(root, USER_BASE, false) == old,
               "memory test: mapping rollback leaked or changed old leaf");
        pte_t *leaf = vm_getpte(root, (1ul << 30) - PGSIZE, false);
        assert(leaf == NULL || !(*leaf & PTE_V), "memory test: partial leaf remains");
    }
    for (int count = 0; count < 2; count++)
    {
        pmem_test_fail_after(count);
        pte_t *leaf = vm_getpte(root, 2ul << 30, true);
        pmem_test_fail_after(-1);
        assert(leaf == NULL && pmem_available(true) == baseline,
               "memory test: walker rollback leak");
    }
    assert(vm_mappages(root, USER_BASE, page, PGSIZE, PTE_R) == -1 &&
           vm_mappages(root, 0, page, PGSIZE, PTE_W) == -1 &&
           vm_mappages(root, VA_MAX - PGSIZE, page, PGSIZE + 1, PTE_R) == -1 &&
           *vm_getpte(root, USER_BASE, false) == old, "memory test: invalid map accepted");
    assert(vm_mappages(root, 2ul << 30, page, PGSIZE, PTE_R) == 0,
           "memory test: distant mapping failed");
    pte_t distant = *vm_getpte(root, 2ul << 30, false);
    baseline = pmem_available(true);
    pmem_test_fail_after(1);
    int result = vm_mappages(root, (1ul << 30) - PGSIZE, page, 2 * PGSIZE, PTE_R);
    pmem_test_fail_after(-1);
    assert(result == -1 && pmem_available(true) == baseline &&
           *vm_getpte(root, 2ul << 30, false) == distant &&
           *vm_getpte(root, USER_BASE, false) == old,
           "memory test: range rollback changed unrelated subtree");
    assert(vm_mappages(root, (1ul << 30) - PGSIZE, page, 2 * PGSIZE, PTE_R) == 0,
           "memory test: cross-root mapping failed");
    assert(*vm_getpte(root, 2ul << 30, false) == distant,
           "memory test: range commit changed unrelated subtree");
    vm_freewalk(root);
    pmem_free(page, false);
    assert(pmem_available(true) == before, "memory test: final leak");
    printf("lab-4 memory failure tests passed\n");
}
#endif
