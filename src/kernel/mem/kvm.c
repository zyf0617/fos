#include "mod.h"
#include "../trap/type.h"

// 内核页表
static pgtbl_t kernel_pgtbl;

// 根据pagetable,找到va对应的pte
// 若设置alloc=true 则在PTE无效时尝试申请一个物理页
// 成功返回PTE, 失败返回NULL
// 提示：使用 VA_TO_VPN + PTE_TO_PA + PA_TO_PTE
pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    if (pgtbl == NULL || va >= VA_MAX)
        return NULL;

    for (int level = 2; level > 0; level--)
    {
        pte_t *pte = &pgtbl[VA_TO_VPN(va, level)];

        if (*pte & PTE_V)
        {
            // 中间级PTE必须指向下一级页表，不能是叶子映射。
            if (!PTE_CHECK(*pte))
                return NULL;
            pgtbl = (pgtbl_t)PTE_TO_PA(*pte);
        }
        else
        {
            if (!alloc)
                return NULL;
            pgtbl_t next = (pgtbl_t)pmem_alloc(true);
            *pte = PA_TO_PTE(next) | PTE_V;
            pgtbl = next;
        }
    }

    return &pgtbl[VA_TO_VPN(va, 0)];
}

// 在pgtbl中建立 [va, va + len) -> [pa, pa + len) 的映射
// 本质是找到va在页表对应位置的pte并修改它
// 检查: va pa 应当是 page-aligned, len(字节数) > 0, va + len <= VA_MAX
// 注意: perm 应该如何使用
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    assert(pgtbl != NULL, "vm_mappages: null pagetable");
    assert((va % PGSIZE) == 0, "vm_mappages: va is not page aligned");
    assert((pa % PGSIZE) == 0, "vm_mappages: pa is not page aligned");
    assert(len > 0, "vm_mappages: empty range");
    assert(va < VA_MAX && len <= VA_MAX - va,
           "vm_mappages: virtual range overflow");
    assert((perm & PTE_V) == 0, "vm_mappages: perm contains PTE_V");
    assert((perm & (PTE_R | PTE_W | PTE_X)) != 0,
           "vm_mappages: invalid leaf permissions");

    uint64 last = (va + len - 1) & ~(PGSIZE - 1);
    for (;;)
    {
        pte_t *pte = vm_getpte(pgtbl, va, true);
        assert(pte != NULL, "vm_mappages: cannot allocate pte");
        assert((*pte & PTE_V) == 0, "vm_mappages: remap");
        *pte = PA_TO_PTE(pa) | perm | PTE_V;

        if (va == last)
            break;
        va += PGSIZE;
        pa += PGSIZE;
    }
}

// 解除pgtbl中[va, va+len)区域的映射
// 如果freeit == true则释放对应物理页, 默认是用户的物理页
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    assert(pgtbl != NULL, "vm_unmappages: null pagetable");
    assert((va % PGSIZE) == 0, "vm_unmappages: va is not page aligned");
    assert(len > 0, "vm_unmappages: empty range");
    assert(va < VA_MAX && len <= VA_MAX - va,
           "vm_unmappages: virtual range overflow");

    uint64 last = (va + len - 1) & ~(PGSIZE - 1);
    for (;;)
    {
        pte_t *pte = vm_getpte(pgtbl, va, false);
        assert(pte != NULL && (*pte & PTE_V),
               "vm_unmappages: page is not mapped");
        assert(!PTE_CHECK(*pte), "vm_unmappages: pte is not a leaf");

        if (freeit)
            pmem_free(PTE_TO_PA(*pte), false);
        *pte = 0;

        if (va == last)
            break;
        va += PGSIZE;
    }
}

// 完成UART、PLIC、内核代码区、内核数据区、可分配区域的页表映射
// CLINT属于M-mode资源，OpenSBI版内核通过SBI管理timer/IPI，不直接映射。
// 相当于部分填充kernel_pgtbl
void kvm_init()
{
    kernel_pgtbl = (pgtbl_t)pmem_alloc(true);

    // MMIO设备区域使用恒等映射。CLINT由OpenSBI管理，不在S-mode映射。
    vm_mappages(kernel_pgtbl, UART_BASE, UART_BASE, PGSIZE,
                PTE_R | PTE_W);
    vm_mappages(kernel_pgtbl, PLIC_BASE, PLIC_BASE, PLIC_SIZE,
                PTE_R | PTE_W);

    // 代码和只读数据可读可执行，但不可写。
    vm_mappages(kernel_pgtbl, KERNEL_BASE, KERNEL_BASE,
                (uint64)KERNEL_DATA - KERNEL_BASE, PTE_R | PTE_X);

    // 内核数据与后续可分配RAM可读可写，不可执行。
    vm_mappages(kernel_pgtbl, (uint64)KERNEL_DATA, (uint64)KERNEL_DATA,
                PHYS_MEM_END - (uint64)KERNEL_DATA, PTE_R | PTE_W);
}

// 每个CPU都需要调用, 从不使用页表切换到使用内核页表
// 切换后需要刷新TLB里面的缓存
void kvm_inithart()
{
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
