#include "mod.h"

// 内核页池和用户页池共用同一套分配器不变式，但必须分别验证。
static void check_page_pool(bool in_kernel)
{
    uint8 *page = (uint8 *)pmem_alloc(in_kernel);
    assert(((uint64)page % PGSIZE) == 0,
           "check_page_pool: unaligned page");

    for (uint32 i = 0; i < PGSIZE; i++)
        assert(page[i] == 0, "check_page_pool: page is not zeroed");

    page[0] = 0x5a;
    page[PGSIZE - 1] = 0xa5;
    pmem_free((uint64)page, in_kernel);

    page = (uint8 *)pmem_alloc(in_kernel);
    for (uint32 i = 0; i < PGSIZE; i++)
        assert(page[i] == 0, "check_page_pool: reused page is not zeroed");
    pmem_free((uint64)page, in_kernel);
}

// 验证物理页分配的对齐、清零、回收和页池隔离语义。
static void pmem_self_test(void)
{
    check_page_pool(true);
    check_page_pool(false);
}

// 验证Sv39三级页表的建表、叶子PTE内容、解映射和用户页回收。
static void vm_self_test(void)
{
    const uint64 va = 0x4000;
    const uint64 far_va = 512 * PGSIZE;
    pgtbl_t root = (pgtbl_t)pmem_alloc(true);
    uint64 user_page_1 = (uint64)pmem_alloc(false);
    uint64 user_page_2 = (uint64)pmem_alloc(false);

    // alloc=false 查询不得隐式扩展页表。
    assert(vm_getpte(root, va, false) == NULL,
           "vm_self_test: lookup unexpectedly allocated");

    vm_mappages(root, va, user_page_1, PGSIZE,
                PTE_R | PTE_W | PTE_U);
    // 跨过一个 VPN[0] 范围，覆盖新建第二个最低级页表的路径。
    vm_mappages(root, far_va, user_page_2, PGSIZE, PTE_R | PTE_X);

    pte_t *leaf = vm_getpte(root, va, false);
    assert(leaf != NULL, "vm_self_test: missing leaf pte");
    assert((*leaf & PTE_V) != 0, "vm_self_test: invalid leaf pte");
    assert(PTE_TO_PA(*leaf) == user_page_1,
           "vm_self_test: physical address mismatch");
    assert((*leaf & (PTE_R | PTE_W | PTE_U)) ==
               (PTE_R | PTE_W | PTE_U),
           "vm_self_test: permission mismatch");

    pte_t *far_leaf = vm_getpte(root, far_va, false);
    assert(far_leaf != NULL && (*far_leaf & PTE_V),
           "vm_self_test: missing far leaf pte");
    assert(PTE_TO_PA(*far_leaf) == user_page_2,
           "vm_self_test: far physical address mismatch");

    // 先保存中间页表的地址，自检结束后将其一并回收。
    pgtbl_t level1 = (pgtbl_t)PTE_TO_PA(root[VA_TO_VPN(va, 2)]);
    pgtbl_t level0_near =
        (pgtbl_t)PTE_TO_PA(level1[VA_TO_VPN(va, 1)]);
    pgtbl_t level0_far =
        (pgtbl_t)PTE_TO_PA(level1[VA_TO_VPN(far_va, 1)]);

    vm_unmappages(root, va, PGSIZE, true);
    vm_unmappages(root, far_va, PGSIZE, true);
    assert((*leaf & PTE_V) == 0, "vm_self_test: leaf still mapped");
    assert((*far_leaf & PTE_V) == 0,
           "vm_self_test: far leaf still mapped");

    pmem_free((uint64)level0_near, true);
    pmem_free((uint64)level0_far, true);
    pmem_free((uint64)level1, true);
    pmem_free((uint64)root, true);
}

void mem_self_test(void)
{
    pmem_self_test();
    vm_self_test();
    printf("lab-2 memory self-test passed\n");
}
