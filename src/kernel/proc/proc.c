#include "mod.h"
#include "../trap/mod.h"
#include "initcode.h"
#include "../agent/mod.h"

extern char trampoline[];
extern void swtch(context_t *old, context_t *new);

typedef struct first_resources {
    proc_t proc;
} first_resources_t;
static first_resources_t first;

pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    assert(trapframe != 0 && trapframe % PGSIZE == 0,
           "proc_pgtbl_init: invalid trapframe");
    pgtbl_t root = pmem_alloc(true);
    if (root == NULL)
        return NULL;
    if (vm_mappages(root, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X) ||
        vm_mappages(root, TRAPFRAME, trapframe, PGSIZE, PTE_R | PTE_W))
    {
        vm_freewalk(root);
        return NULL;
    }
    return root;
}

void proc_release_resources(proc_t *p)
{
    assert(p && myproc() != p, "proc_release: process still current");
    // 调用者保证该地址空间已停止使用；预映射的内核栈由运行层持有。
    uvm_destroy_pgtbl(p->pgtbl);
#if AEGIS
    agent_detach(p);
#endif
    if (p->tf)
        pmem_free((uint64)p->tf, true);
    while (p->mmap)
    {
        mmap_region_t *node = p->mmap;
        p->mmap = node->next;
        mmap_region_free(node);
    }
    memset(p, 0, sizeof(*p));
}

#if AEGIS
static uint64 user_pages(pgtbl_t root, int level)
{
    uint64 count = 0;
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = root[i];
        if (!(pte & PTE_V)) continue;
        if (PTE_CHECK(pte))
        {
            assert(level > 0, "proc_clone: malformed page table");
            count += user_pages((pgtbl_t)PTE_TO_PA(pte), level - 1);
        }
        else if (pte & PTE_U)
        {
            assert(level == 0, "proc_clone: unexpected huge page");
            count++;
        }
    }
    return count;
}
#endif

int proc_clone_resources(proc_t *dst, const proc_t *src, int pid)
{
    assert(dst && src && dst != src && src->pgtbl && src->tf,
           "proc_clone: invalid owner");
    for (uint64 i = 0; i < sizeof(*dst); i++)
        assert(!((uint8 *)dst)[i], "proc_clone: destination not empty");
    dst->pid = pid;
#if AEGIS
    // 源由调用者稳定持有；先在同一组锁内预留成员和整批数据页。
    if (src->agent && agent_attach(dst, src->agent, user_pages(src->pgtbl, 2)))
        goto failed;
#endif
    dst->tf = pmem_alloc(true);
    if (!dst->tf) goto failed;
    dst->pgtbl = proc_pgtbl_init((uint64)dst->tf);
    if (!dst->pgtbl) goto failed;
    mmap_region_t **tail = &dst->mmap;
    for (mmap_region_t *r = src->mmap; r; r = r->next)
    {
        mmap_region_t *node = mmap_region_alloc();
        if (!node) goto failed;
        *node = *r;
        node->next = NULL;
        *tail = node;
        tail = &node->next;
    }
    if (uvm_copy_pgtbl(src->pgtbl, dst->pgtbl, src->heap_top,
                       src->ustack_npage, src->mmap))
        goto failed;
    dst->heap_top = src->heap_top;
    dst->ustack_npage = src->ustack_npage;
    *dst->tf = *src->tf;
    dst->tf->a0 = 0;
    dst->tf->user_to_kern_satp = 0;
    dst->tf->user_to_kern_sp = 0;
    dst->tf->user_to_kern_trapvector = 0;
    dst->tf->user_to_kern_hartid = 0;
    // kstack/ctx 保持零：资源准备成功不等于可被调度运行。
    return 0;
failed:
    proc_release_resources(dst);
    return -1;
}

static bool construct_first(first_resources_t *r)
{
    memset(r, 0, sizeof(*r));
    proc_t *p = &r->proc;
    p->tf = pmem_alloc(true);
    if (p->tf == NULL)
        goto failed;
    p->pgtbl = proc_pgtbl_init((uint64)p->tf);
    if (p->pgtbl == NULL)
        goto failed;
    uint64 code = (uint64)pmem_alloc(false);
    if (!code)
        goto failed;
    if (vm_mappages(p->pgtbl, USER_BASE, code, PGSIZE, PTE_R | PTE_X | PTE_U))
    {
        pmem_free(code, false);
        goto failed;
    }
    uint64 stack = (uint64)pmem_alloc(false);
    if (!stack)
        goto failed;
    if (vm_mappages(p->pgtbl, USER_STACK_TOP - PGSIZE, stack, PGSIZE, PTE_R | PTE_W | PTE_U))
    {
        pmem_free(stack, false);
        goto failed;
    }
    assert(target_user_initcode_len > 0 && target_user_initcode_len <= PGSIZE,
           "proc_make_first: invalid initcode size");
    memmove((void *)code, target_user_initcode, target_user_initcode_len);
    // 新写入的代码页会由取指单元使用。
    asm volatile("fence.i" ::: "memory");
    p->pid = 0;
    p->heap_top = USER_BASE + PGSIZE;
    p->ustack_npage = 1;
    p->tf->user_to_kern_epc = USER_BASE;
    p->tf->sp = TRAPFRAME;
    p->kstack = KSTACK(0);
    p->ctx.ra = (uint64)trap_user_return;
    p->ctx.sp = p->kstack + PGSIZE;
    return true;
failed:
    proc_release_resources(&r->proc);
    return false;
}

void proc_make_first(void)
{
    cpu_t *cpu = mycpu();
    assert((uint64)mycpuid() == boot_hartid && cpu->proc == NULL, "proc_make_first: invalid owner");
    int was_enabled = intr_get();
    intr_off();
    uint32 kernel_before = pmem_available(true);
    uint32 user_before = pmem_available(false);
    uint32 nodes_before = mmap_available();
    if (!construct_first(&first))
    {
        printf("lab-4 first process allocation failed\n");
        if (was_enabled)
            intr_on();
        return;
    }
#if AEGIS
    if (agent_attach_boot(&first.proc))
    {
        proc_release_resources(&first.proc);
        if (was_enabled) intr_on();
        printf("aegis boot policy rejected process\n");
        return;
    }
#endif
#if PROC_LIFECYCLE
    proc_lifecycle_run(&first.proc);
#else
    cpu->proc = &first.proc;
    printf("lab-4 entering user mode\n");
    swtch(&cpu->ctx, &first.proc.ctx);
    // 故障进程不再调度，当前已恢复到CPU早期栈与内核页表。
    cpu->proc = NULL;
    proc_release_resources(&first.proc);
#endif
    assert(pmem_available(false) == user_before,
           "proc_make_first: leaked user resources");
    assert(pmem_available(true) == kernel_before,
           "proc_make_first: leaked kernel resources");
    assert(mmap_available() == nodes_before, "proc_make_first: leaked mmap nodes");
#if PROC_LIFECYCLE
    printf("aegis process lifecycle cleaned up\n");
#else
    printf("lab-4 faulted process cleaned up\n");
#endif
#if AEGIS
    agent_boot_check();
    agent_audit_dump();
#endif
    if (was_enabled)
        intr_on();
}

void proc_return(void)
{
#if PROC_LIFECYCLE
    proc_exit((uint64)-1);
#endif
    intr_off();
    cpu_t *cpu = mycpu();
    assert(cpu->proc != NULL, "proc_return: no current process");
    swtch(&cpu->proc->ctx, &cpu->ctx);
    panic("proc_return: faulted process resumed");
    __builtin_unreachable();
}

#if defined(LAB4_TEST) || defined(LAB5_TEST)
void proc_self_test(void)
{
    uint32 kernel_before = pmem_available(true), user_before = pmem_available(false);
    bool completed = false;
    for (int count = 0; count < 16; count++)
    {
        pmem_test_fail_after(count);
        bool success = construct_first(&first);
        pmem_test_fail_after(-1);
        if (success)
        {
            pgtbl_t root = first.proc.pgtbl;
            assert(vm_getpte(root, 0, false) == NULL ||
                   !(*vm_getpte(root, 0, false) & PTE_V), "proc test: zero mapped");
            assert(PTE_FLAGS(*vm_getpte(root, USER_BASE, false)) ==
                   (PTE_V | PTE_R | PTE_X | PTE_U | PTE_MACHINE), "proc test: code permissions");
            assert(PTE_FLAGS(*vm_getpte(root, TRAPFRAME - PGSIZE, false)) ==
                   (PTE_V | PTE_R | PTE_W | PTE_U | PTE_MACHINE), "proc test: stack permissions");
            assert(PTE_FLAGS(*vm_getpte(root, TRAMPOLINE, false)) ==
                   (PTE_V | PTE_R | PTE_X | PTE_MACHINE), "proc test: trampoline exposed");
            assert(PTE_FLAGS(*vm_getpte(root, TRAPFRAME, false)) ==
                   (PTE_V | PTE_R | PTE_W | PTE_MACHINE), "proc test: trapframe exposed");
            proc_release_resources(&first.proc);
            completed = true;
        }
        assert(myproc() == NULL && mycpu()->noff == 0 && pmem_available(true) == kernel_before &&
               pmem_available(false) == user_before, "proc test: partial publication or leak");
        if (success)
            break;
    }
    assert(completed, "proc test: no successful construction");
    printf("lab-4 process rollback test passed\n");
}
#endif
