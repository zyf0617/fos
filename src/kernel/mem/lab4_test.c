#include "mod.h"
#ifdef LAB4_TEST
static void *pages[KERN_PAGES];
void lab4_memory_test(void)
{
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
    vm_freewalk(root);
    pmem_free(page, false);
    assert(pmem_available(true) == before, "memory test: final leak");
    printf("lab-4 memory failure tests passed\n");
}
#endif
