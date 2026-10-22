#include "mod.h"
#include "../trap/type.h"

static pgtbl_t kernel_pgtbl;
extern char trampoline[];

// 独占修改期间用RSW标记本次新建的中间页；提交后清除，失败时回收。
#define PTE_NEW (1ul << 8)
static pte_t *walk(pgtbl_t root, uint64 va, bool alloc, bool transaction)
{
    if (root == NULL || va >= VA_MAX)
        return NULL;
    pte_t *created[2];
    int count = 0;
    for (int level = 2; level > 0; level--)
    {
        pte_t *pte = &root[VA_TO_VPN(va, level)];
        if (*pte & PTE_V)
        {
            assert(PTE_CHECK(*pte), "vm_getpte: upper-level leaf");
        }
        else
        {
            if (!alloc)
                return NULL;
            pgtbl_t next = pmem_alloc(true);
            if (next == NULL)
            {
                while (count > 0)
                {
                    pte_t *entry = created[--count];
                    pmem_free(PTE_TO_PA(*entry), true);
                    *entry = 0;
                }
                return NULL;
            }
            *pte = PA_TO_PTE(next) | PTE_V | (transaction ? PTE_NEW : 0);
            created[count++] = pte;
        }
        root = (pgtbl_t)PTE_TO_PA(*pte);
    }
    return &root[VA_TO_VPN(va, 0)];
}

pte_t *vm_getpte(pgtbl_t root, uint64 va, bool alloc)
{
    return walk(root, va, alloc, false);
}

static void finish_mapping(pgtbl_t root, int level, uint64 first, uint64 last, bool commit)
{
    if (level == 0)
        return;
    uint64 low = VA_TO_VPN(first, level), high = VA_TO_VPN(last, level);
    for (uint64 i = low; i <= high; i++)
    {
        pte_t *entry = &root[i];
        if (!(*entry & PTE_V) || !PTE_CHECK(*entry))
            continue;
        // 每层只访问本次范围覆盖的槽；跨边界时中间子树覆盖完整VPN范围。
        uint64 span = 1ul << VA_SHIFT(level);
        uint64 child_first = i == low ? first : 0;
        uint64 child_last = i == high ? last : span - 1;
        finish_mapping((pgtbl_t)PTE_TO_PA(*entry), level - 1,
                       child_first, child_last, commit);
        if (*entry & PTE_NEW)
        {
            if (commit)
                *entry &= ~PTE_NEW;
            else
            {
                pmem_free(PTE_TO_PA(*entry), true);
                *entry = 0;
            }
        }
    }
}

int vm_mappages(pgtbl_t root, uint64 va, uint64 pa, uint64 len, int perm)
{
    const uint64 pa_limit = 1ul << 56;
    if (root == NULL || va % PGSIZE || pa % PGSIZE || len == 0 ||
        va >= VA_MAX || len > VA_MAX - va ||
        (perm & ~(PTE_R | PTE_W | PTE_X | PTE_U)) ||
        !(perm & (PTE_R | PTE_X)) || ((perm & PTE_W) && !(perm & PTE_R)))
        return -1;
    uint64 size = (len + PGSIZE - 1) & ~(PGSIZE - 1);
    if (size > VA_MAX - va || pa >= pa_limit || size > pa_limit - pa)
        return -1;
    // 修改之前检查整个范围，避免在覆盖已有映射时留下部分新映射。
    for (uint64 offset = 0; offset < size; offset += PGSIZE)
    {
        pte_t *pte = walk(root, va + offset, false, false);
        if (pte != NULL && (*pte & PTE_V))
            return -1;
    }
    uint64 done = 0;
    for (; done < size; done += PGSIZE)
    {
        pte_t *pte = walk(root, va + done, true, true);
        if (pte == NULL)
            break;
        *pte = PA_TO_PTE(pa + done) | perm | PTE_V | PTE_MACHINE;
    }
    bool success = done == size;
    if (!success)
        for (uint64 offset = 0; offset < done; offset += PGSIZE)
            *walk(root, va + offset, false, false) = 0;
    finish_mapping(root, 2, va, va + size - 1, success);
    return success ? 0 : -1;
}

void vm_unmappages(pgtbl_t root, uint64 va, uint64 len, bool freeit)
{
    assert(root != NULL && va % PGSIZE == 0 && len > 0 &&
           va < VA_MAX && len <= VA_MAX - va, "vm_unmappages: invalid range");
    uint64 size = (len + PGSIZE - 1) & ~(PGSIZE - 1);
    assert(size <= VA_MAX - va, "vm_unmappages: rounded range overflow");
    // 先检查全部叶项，所有权错误不得产生部分解除。
    for (uint64 offset = 0; offset < size; offset += PGSIZE)
    {
        pte_t *pte = vm_getpte(root, va + offset, false);
        assert(pte != NULL && (*pte & PTE_V) && !PTE_CHECK(*pte),
               "vm_unmappages: missing leaf");
    }
    for (uint64 offset = 0; offset < size; offset += PGSIZE)
    {
        pte_t *pte = vm_getpte(root, va + offset, false);
        uint64 page = PTE_TO_PA(*pte);
        *pte = 0;
        sfence_vma();
        if (freeit)
            pmem_free(page, check_inkernel(page));
    }
}

void vm_freewalk(pgtbl_t root)
{
    if (root == NULL)
        return;
    for (int i = 0; i < 512; i++)
        if ((root[i] & PTE_V) && PTE_CHECK(root[i]))
            vm_freewalk((pgtbl_t)PTE_TO_PA(root[i]));
    pmem_free((uint64)root, true);
}

void kvm_init(void)
{
    pgtbl_t root = pmem_alloc(true);
    uint64 stack = (uint64)pmem_alloc(true);
    if (root == NULL || stack == 0 ||
        vm_mappages(root, UART_BASE, UART_BASE, PGSIZE, PTE_R | PTE_W) ||
        vm_mappages(root, PLIC_BASE, PLIC_BASE, PLIC_SIZE, PTE_R | PTE_W) ||
        vm_mappages(root, KERNEL_BASE, KERNEL_BASE,
                    (uint64)KERNEL_DATA - KERNEL_BASE, PTE_R | PTE_X) ||
        vm_mappages(root, (uint64)KERNEL_DATA, (uint64)KERNEL_DATA,
                    PHYS_MEM_END - (uint64)KERNEL_DATA, PTE_R | PTE_W) ||
        vm_mappages(root, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X) ||
        vm_mappages(root, KSTACK(0), stack, PGSIZE, PTE_R | PTE_W))
    {
        vm_freewalk(root);
        if (stack)
            pmem_free(stack, true);
        panic("kvm_init: cannot construct kernel mappings");
    }
    kernel_pgtbl = root;
}

void kvm_inithart(void)
{
    assert(kernel_pgtbl != NULL, "kvm_inithart: unpublished pagetable");
    sfence_vma();
    w_satp(MAKE_SATP(kernel_pgtbl));
    sfence_vma();
}

// 输出页表内容(for debug)
void vm_print(pgtbl_t pgtbl)
{
    // 顶级页表，次级页表，低级页表
    pgtbl_t pgtbl_2 = pgtbl, pgtbl_1 = NULL, pgtbl_0 = NULL;
    pte_t pte;

    printf("level-2 pgtbl: pa = %p\n", pgtbl_2);
    for (int i = 0; i < PGSIZE / sizeof(pte_t); i++)
    {
        pte = pgtbl_2[i];
        if (!((pte)&PTE_V))
            continue;
        assert(PTE_CHECK(pte), "vm_print: pte check fail (1)");
        pgtbl_1 = (pgtbl_t)PTE_TO_PA(pte);
        printf(".. level-1 pgtbl %d: pa = %p\n", i, pgtbl_1);

        for (int j = 0; j < PGSIZE / sizeof(pte_t); j++)
        {
            pte = pgtbl_1[j];
            if (!((pte)&PTE_V))
                continue;
            assert(PTE_CHECK(pte), "vm_print: pte check fail (2)");
            pgtbl_0 = (pgtbl_t)PTE_TO_PA(pte);
            printf(".. .. level-0 pgtbl %d: pa = %p\n", j, pgtbl_0);

            for (int k = 0; k < PGSIZE / sizeof(pte_t); k++)
            {
                pte = pgtbl_0[k];
                if (!((pte)&PTE_V))
                    continue;
                assert(!PTE_CHECK(pte), "vm_print: pte check fail (3)");
                printf(".. .. .. physical page %d: pa = %p flags = %d\n", k, (uint64)PTE_TO_PA(pte), (int)PTE_FLAGS(pte));
            }
        }
    }
}

// src已经完整准备，且与dst没有叶项冲突；提交过程不再申请资源。
static void merge_user_tree(pgtbl_t dst, pgtbl_t src, int level)
{
    for (int i = 0; i < 512; i++)
    {
        if (!(src[i] & PTE_V))
            continue;
        if (!(dst[i] & PTE_V))
        {
            dst[i] = src[i];
            src[i] = 0;
        }
        else
        {
            assert(level > 0 && PTE_CHECK(dst[i]) && PTE_CHECK(src[i]),
                   "vm_merge_user: conflicting leaf");
            merge_user_tree((pgtbl_t)PTE_TO_PA(dst[i]),
                            (pgtbl_t)PTE_TO_PA(src[i]), level - 1);
        }
    }
}

void vm_merge_user(pgtbl_t dst, pgtbl_t src)
{
    assert(dst && src && dst != src, "vm_merge_user: invalid trees");
    merge_user_tree(dst, src, 2);
    sfence_vma();
}
