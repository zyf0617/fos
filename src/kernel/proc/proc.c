#include "mod.h"
#include "../trap/mod.h"
#include "initcode.h"

extern char trampoline[];
extern void swtch(context_t *old, context_t *new);

typedef struct first_resources {
    proc_t proc;
    uint64 code;
    uint64 stack;
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

static void release_first(first_resources_t *r)
{
    // 调用时用户页表已不活动；freewalk只回收页表，不释放共享trampoline。
    vm_freewalk(r->proc.pgtbl);
    if (r->proc.tf)
        pmem_free((uint64)r->proc.tf, true);
    if (r->code)
        pmem_free(r->code, false);
    if (r->stack)
        pmem_free(r->stack, false);
    memset(r, 0, sizeof(*r));
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
    r->code = (uint64)pmem_alloc(false);
    r->stack = (uint64)pmem_alloc(false);
    if (!r->code || !r->stack ||
        vm_mappages(p->pgtbl, USER_BASE, r->code, PGSIZE, PTE_R | PTE_X | PTE_U) ||
        vm_mappages(p->pgtbl, TRAPFRAME - PGSIZE, r->stack, PGSIZE, PTE_R | PTE_W | PTE_U))
        goto failed;
    assert(target_user_initcode_len > 0 && target_user_initcode_len <= PGSIZE,
           "proc_make_first: invalid initcode size");
    memmove((void *)r->code, target_user_initcode, target_user_initcode_len);
    // 新写入的代码页会由取指单元使用。
    asm volatile("fence.i" ::: "memory");
    p->pid = FIRST_HART;
    p->heap_top = USER_BASE + PGSIZE;
    p->ustack_npage = 1;
    p->tf->user_to_kern_epc = USER_BASE;
    p->tf->sp = TRAPFRAME;
    p->kstack = KSTACK(0);
    p->ctx.ra = (uint64)trap_user_return;
    p->ctx.sp = p->kstack + PGSIZE;
    return true;
failed:
    release_first(r);
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
    if (!construct_first(&first))
    {
        printf("lab-4 first process allocation failed\n");
        if (was_enabled)
            intr_on();
        return;
    }
    cpu->proc = &first.proc;
    printf("lab-4 entering user mode\n");
    swtch(&cpu->ctx, &first.proc.ctx);
    // 故障进程不再调度，当前已恢复到CPU早期栈与内核页表。
    cpu->proc = NULL;
    release_first(&first);
    assert(pmem_available(false) == user_before,
           "proc_make_first: leaked user resources");
    assert(pmem_available(true) == kernel_before,
           "proc_make_first: leaked kernel resources");
    printf("lab-4 faulted process cleaned up\n");
    if (was_enabled)
        intr_on();
}

void proc_return(void)
{
    intr_off();
    cpu_t *cpu = mycpu();
    assert(cpu->proc != NULL, "proc_return: no current process");
    swtch(&cpu->proc->ctx, &cpu->ctx);
    panic("proc_return: faulted process resumed");
    __builtin_unreachable();
}

#ifdef LAB4_TEST
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
                   (PTE_V | PTE_R | PTE_X | PTE_U), "proc test: code permissions");
            assert(PTE_FLAGS(*vm_getpte(root, TRAPFRAME - PGSIZE, false)) ==
                   (PTE_V | PTE_R | PTE_W | PTE_U), "proc test: stack permissions");
            assert(PTE_FLAGS(*vm_getpte(root, TRAMPOLINE, false)) ==
                   (PTE_V | PTE_R | PTE_X), "proc test: trampoline exposed");
            assert(PTE_FLAGS(*vm_getpte(root, TRAPFRAME, false)) ==
                   (PTE_V | PTE_R | PTE_W), "proc test: trapframe exposed");
            release_first(&first);
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
