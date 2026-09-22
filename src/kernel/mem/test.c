#include "mod.h"

// 验证物理页分配的对齐、清零和回收语义。
static void pmem_self_test(void)
{
    uint8 *page = (uint8 *)pmem_alloc(true);
    assert(((uint64)page % PGSIZE) == 0,
           "pmem_self_test: unaligned page");

    for (uint32 i = 0; i < PGSIZE; i++)
        assert(page[i] == 0, "pmem_self_test: page is not zeroed");

    page[0] = 0x5a;
    page[PGSIZE - 1] = 0xa5;
    pmem_free((uint64)page, true);

    page = (uint8 *)pmem_alloc(true);
    for (uint32 i = 0; i < PGSIZE; i++)
        assert(page[i] == 0, "pmem_self_test: reused page is not zeroed");
    pmem_free((uint64)page, true);
}

// 验证Sv39三级页表的建表、叶子PTE内容、解映射和用户页回收。
static void vm_self_test(void)
{
    const uint64 va = 0x4000;
    pgtbl_t root = (pgtbl_t)pmem_alloc(true);
    uint64 user_page = (uint64)pmem_alloc(false);

    vm_mappages(root, va, user_page, PGSIZE, PTE_R | PTE_W);

    pte_t *leaf = vm_getpte(root, va, false);
    assert(leaf != NULL, "vm_self_test: missing leaf pte");
    assert((*leaf & PTE_V) != 0, "vm_self_test: invalid leaf pte");
    assert(PTE_TO_PA(*leaf) == user_page,
           "vm_self_test: physical address mismatch");
    assert((*leaf & (PTE_R | PTE_W)) == (PTE_R | PTE_W),
           "vm_self_test: permission mismatch");

    // 先保存两级中间页表的地址，自检结束后将其一并回收。
    pgtbl_t level1 = (pgtbl_t)PTE_TO_PA(root[VA_TO_VPN(va, 2)]);
    pgtbl_t level0 =
        (pgtbl_t)PTE_TO_PA(level1[VA_TO_VPN(va, 1)]);

    vm_unmappages(root, va, PGSIZE, true);
    assert((*leaf & PTE_V) == 0, "vm_self_test: leaf still mapped");

    pmem_free((uint64)level0, true);
    pmem_free((uint64)level1, true);
    pmem_free((uint64)root, true);
}

void mem_self_test(void)
{
    pmem_self_test();
    vm_self_test();
    printf("lab-2 memory self-test passed\n");
}
