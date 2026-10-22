#include "mod.h"

static uint64 round_up(uint64 value) { return (value + PGSIZE - 1) & ~(PGSIZE - 1ul); }
static uint64 round_down(uint64 value) { return value & ~(PGSIZE - 1ul); }

static uint64 user_pa(pgtbl_t root, uint64 address, int required)
{
    if (address >= VA_MAX)
        return 0;
    pte_t *pte = vm_getpte(root, address, false);
    if (!pte || (*pte & (PTE_V | PTE_U | required)) != (PTE_V | PTE_U | required) || PTE_CHECK(*pte))
        return 0;
    return PTE_TO_PA(*pte) + address % PGSIZE;
}

static int user_copy(pgtbl_t root, uint64 kernel, uint64 user, uint32 len, bool out)
{
    if (len == 0)
        return 0;
    if (user >= VA_MAX || len > VA_MAX - user)
        return -1;
    while (len)
    {
        uint64 pa = user_pa(root, user, out ? PTE_W : PTE_R);
        if (!pa)
            return -1;
        uint32 count = PGSIZE - user % PGSIZE;
        if (count > len)
            count = len;
        if (out)
            memmove((void *)pa, (void *)kernel, count);
        else
            memmove((void *)kernel, (void *)pa, count);
        user += count;
        kernel += count;
        len -= count;
    }
    return 0;
}

int uvm_copyin(pgtbl_t root, uint64 dst, uint64 src, uint32 len)
{
    return user_copy(root, dst, src, len, false);
}

int uvm_copyout(pgtbl_t root, uint64 dst, uint64 src, uint32 len)
{
    return user_copy(root, src, dst, len, true);
}

int uvm_copyin_str(pgtbl_t root, uint64 dst, uint64 src, uint32 maxlen)
{
    while (maxlen && src < VA_MAX)
    {
        uint64 pa = user_pa(root, src, PTE_R);
        if (!pa)
            return -1;
        uint32 count = PGSIZE - src % PGSIZE;
        if (count > maxlen)
            count = maxlen;
        for (uint32 i = 0; i < count; i++)
        {
            char c = *(char *)(pa + i);
            *(char *)dst++ = c;
            if (c == 0)
                return 0;
        }
        src += count;
        maxlen -= count;
    }
    return -1;
}

static void destroy_tree(pgtbl_t root, int level)
{
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = root[i];
        if (!(pte & PTE_V))
            continue;
        if (PTE_CHECK(pte))
        {
            assert(level > 0, "uvm_destroy: invalid bottom pointer");
            destroy_tree((pgtbl_t)PTE_TO_PA(pte), level - 1);
        }
        else if (pte & PTE_U)
        {
            assert(level == 0, "uvm_destroy: unsupported huge page");
            pmem_free(PTE_TO_PA(pte), false);
        }
    }
    pmem_free((uint64)root, true);
}

void uvm_destroy_pgtbl(pgtbl_t root)
{
    if (root)
        destroy_tree(root, 2);
}

// 先在独立临时树里取得全部用户页和页表页；失败不碰目标页表。
static int allocate_range(pgtbl_t root, uint64 begin, uint64 end, int perm)
{
    if (begin == end)
        return 0;
    for (uint64 va = begin; va < end; va += PGSIZE)
    {
        pte_t *pte = vm_getpte(root, va, false);
        if (pte && (*pte & PTE_V))
            return -1;
    }
    pgtbl_t stage = pmem_alloc(true);
    if (!stage)
        return -1;
    for (uint64 va = begin; va < end; va += PGSIZE)
    {
        uint64 page = (uint64)pmem_alloc(false);
        if (!page || vm_mappages(stage, va, page, PGSIZE, perm | PTE_U))
        {
            if (page)
                pmem_free(page, false);
            uvm_destroy_pgtbl(stage);
            return -1;
        }
    }
    vm_merge_user(root, stage);
    vm_freewalk(stage);
    return 0;
}

static bool mapped_range(pgtbl_t root, uint64 begin, uint64 end)
{
    for (uint64 va = begin; va < end; va += PGSIZE)
    {
        pte_t *pte = vm_getpte(root, va, false);
        if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U) || PTE_CHECK(*pte))
            return false;
    }
    return true;
}

uint64 uvm_heap_grow(pgtbl_t root, uint64 top, uint32 len)
{
    if (!root || top < USER_HEAP_BASE || top > MMAP_BEGIN || len > MMAP_BEGIN - top)
        return 0;
    uint64 next = top + len;
    return allocate_range(root, round_up(top), round_up(next), PTE_R | PTE_W) ? 0 : next;
}

uint64 uvm_heap_ungrow(pgtbl_t root, uint64 top, uint32 len)
{
    if (!root || top < USER_HEAP_BASE || top > MMAP_BEGIN || len > top - USER_HEAP_BASE)
        return 0;
    uint64 next = top - len, begin = round_up(next), end = round_up(top);
    if (!mapped_range(root, begin, end))
        return 0;
    if (begin < end)
        vm_unmappages(root, begin, end - begin, true);
    return next;
}

int64 uvm_ustack_grow(pgtbl_t root, uint64 old_pages, uint64 fault)
{
    uint64 max_pages = (USER_STACK_TOP - MMAP_END) / PGSIZE;
    if (!root || !old_pages || old_pages > max_pages || fault < MMAP_END ||
        fault >= USER_STACK_TOP - old_pages * PGSIZE)
        return -1;
    uint64 begin = round_down(fault), end = USER_STACK_TOP - old_pages * PGSIZE;
    if (allocate_range(root, begin, end, PTE_R | PTE_W))
        return -1;
    return (USER_STACK_TOP - begin) / PGSIZE;
}

static uint64 region_end(mmap_region_t *r) { return r->begin + (uint64)r->npages * PGSIZE; }
static bool valid_interval(uint64 begin, uint64 len)
{
    return len && begin % PGSIZE == 0 && len % PGSIZE == 0 &&
           begin >= MMAP_BEGIN && begin < MMAP_END && len <= MMAP_END - begin;
}

uint64 uvm_mmap_find(mmap_region_t *head, uint64 len,
                     mmap_region_t **previous, mmap_region_t **next)
{
    *previous = *next = NULL;
    if (!len || len % PGSIZE || len > MMAP_END - MMAP_BEGIN)
        return 0;
    uint64 begin = MMAP_BEGIN;
    mmap_region_t *prev = NULL;
    for (mmap_region_t *r = head; r; prev = r, r = r->next)
    {
        if (r->begin - begin >= len)
        {
            *previous = prev;
            *next = r;
            return begin;
        }
        begin = region_end(r);
    }
    if (len <= MMAP_END - begin)
    {
        *previous = prev;
        return begin;
    }
    return 0;
}

uint64 uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    proc_t *p = myproc();
    uint64 len = (uint64)npages * PGSIZE;
    if (!npages || (perm & ~(PTE_R | PTE_W | PTE_X | PTE_U)) ||
        !(perm & (PTE_R | PTE_X)) || ((perm & PTE_W) && !(perm & PTE_R)))
        return 0;
    perm = (perm & (PTE_R | PTE_W | PTE_X)) | PTE_U;
    mmap_region_t *prev = NULL, *next = p->mmap;
    if (!begin)
        begin = uvm_mmap_find(p->mmap, len, &prev, &next);
    else
        while (next && next->begin < begin)
        {
            prev = next;
            next = next->next;
        }
    if (!valid_interval(begin, len) || (prev && region_end(prev) > begin) ||
        (next && next->begin < begin + len))
        return 0;
    bool left = prev && region_end(prev) == begin && prev->perm == perm;
    bool right = next && next->begin == begin + len && next->perm == perm;
    mmap_region_t *node = left || right ? NULL : mmap_region_alloc();
    if ((!left && !right && !node) || allocate_range(p->pgtbl, begin, begin + len, perm))
    {
        if (node)
            mmap_region_free(node);
        return 0;
    }
    if (left)
    {
        prev->npages += npages;
        if (right)
        {
            prev->npages += next->npages;
            prev->next = next->next;
            mmap_region_free(next);
        }
    }
    else if (right)
    {
        next->begin = begin;
        next->npages += npages;
    }
    else
    {
        node->begin = begin;
        node->npages = npages;
        node->perm = perm;
        node->next = next;
        if (prev)
            prev->next = node;
        else
            p->mmap = node;
    }
    return begin;
}

int uvm_munmap(uint64 begin, uint32 npages)
{
    proc_t *p = myproc();
    uint64 len = (uint64)npages * PGSIZE;
    if (!valid_interval(begin, len))
        return -1;
    uint64 end = begin + len, cursor = begin;
    mmap_region_t *r = p->mmap;
    while (r && region_end(r) <= begin)
        r = r->next;
    mmap_region_t *first = r;
    while (r && cursor < end)
    {
        if (r->begin > cursor)
            return -1;
        cursor = region_end(r);
        r = r->next;
    }
    if (cursor < end || !mapped_range(p->pgtbl, begin, end))
        return -1;
    mmap_region_t *split = NULL;
    if (first && first->begin < begin && region_end(first) > end)
    {
        split = mmap_region_alloc();
        if (!split)
            return -1;
    }
    vm_unmappages(p->pgtbl, begin, len, true);
    mmap_region_t **link = &p->mmap;
    while ((r = *link) != NULL)
    {
        uint64 old_end = region_end(r);
        if (old_end <= begin) { link = &r->next; continue; }
        if (r->begin >= end) break;
        if (r->begin < begin && old_end > end)
        {
            split->begin = end;
            split->npages = (old_end - end) / PGSIZE;
            split->perm = r->perm;
            split->next = r->next;
            r->npages = (begin - r->begin) / PGSIZE;
            r->next = split;
            break;
        }
        if (r->begin < begin)
        {
            r->npages = (begin - r->begin) / PGSIZE;
            link = &r->next;
        }
        else if (old_end > end)
        {
            r->begin = end;
            r->npages = (old_end - end) / PGSIZE;
            break;
        }
        else
        {
            *link = r->next;
            mmap_region_free(r);
        }
    }
    return 0;
}

void uvm_show_mmaplist(mmap_region_t *head)
{
    for (; head; head = head->next)
        printf("mmap %p ~ %p perm=%x\n", head->begin, region_end(head), head->perm);
}

static bool copy_allowed(uint64 va, uint64 heap, uint64 stack, mmap_region_t *regions)
{
    if (va == USER_BASE || (va >= USER_HEAP_BASE && va < round_up(heap)) ||
        (va >= USER_STACK_TOP - stack * PGSIZE && va < USER_STACK_TOP))
        return true;
    for (; regions; regions = regions->next)
        if (va >= regions->begin && va < region_end(regions))
            return true;
    return false;
}

static int copy_tree(pgtbl_t old, pgtbl_t stage, int level, uint64 prefix,
                     uint64 heap, uint64 stack, mmap_region_t *regions)
{
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = old[i];
        if (!(pte & PTE_V)) continue;
        uint64 va = prefix | ((uint64)i << VA_SHIFT(level));
        if (PTE_CHECK(pte))
        {
            if (!level || copy_tree((pgtbl_t)PTE_TO_PA(pte), stage, level - 1, va, heap, stack, regions))
                return -1;
        }
        else if (pte & PTE_U)
        {
            if (level || !copy_allowed(va, heap, stack, regions))
                return -1;
            uint64 page = (uint64)pmem_alloc(false);
            if (!page) return -1;
            memmove((void *)page, (void *)PTE_TO_PA(pte), PGSIZE);
            int perm = pte & (PTE_R | PTE_W | PTE_X | PTE_U);
            if (vm_mappages(stage, va, page, PGSIZE, perm))
            {
                pmem_free(page, false);
                return -1;
            }
        }
    }
    return 0;
}

static bool has_user(pgtbl_t root, int level)
{
    for (int i = 0; i < 512; i++)
        if (root[i] & PTE_V)
        {
            if (root[i] & PTE_U) return true;
            if (PTE_CHECK(root[i]) && level && has_user((pgtbl_t)PTE_TO_PA(root[i]), level - 1))
                return true;
        }
    return false;
}

int uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap, uint64 stack, mmap_region_t *regions)
{
    if (!old || !new || old == new || heap < USER_HEAP_BASE || heap > MMAP_BEGIN ||
        !stack || stack > (USER_STACK_TOP - MMAP_END) / PGSIZE || has_user(new, 2))
        return -1;
    pgtbl_t stage = pmem_alloc(true);
    if (!stage) return -1;
    if (copy_tree(old, stage, 2, 0, heap, stack, regions))
    {
        uvm_destroy_pgtbl(stage);
        return -1;
    }
    vm_merge_user(new, stage);
    vm_freewalk(stage);
    asm volatile("fence.i" ::: "memory");
    return 0;
}
