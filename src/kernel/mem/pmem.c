#include "mod.h"

// 内核空间和用户空间的可分配物理页分开描述
static alloc_region_t kern_region, user_region;
// 每页的所有权位受其所在区域锁保护，重复释放不能破坏空闲链表。
static uint8 allocated[PHYS_MEM_SIZE / PGSIZE];
#if defined(LAB4_TEST) || defined(LAB5_TEST)
static int fail_after = -1;
void pmem_test_fail_after(int count) { fail_after = count; }
#endif

static void region_init(alloc_region_t *region, uint64 begin, uint64 end,
                        char *name)
{
    assert((begin % PGSIZE) == 0, "region_init: begin is not page aligned");
    assert((end % PGSIZE) == 0, "region_init: end is not page aligned");
    assert(begin <= end, "region_init: invalid range");

    region->begin = begin;
    region->end = end;
    region->allocable = 0;
    region->list_head.next = NULL;
    spinlock_init(&region->lk, name);

    // 头插法建立空闲页链表。页处于空闲状态时，起始处作为next指针。
    for (uint64 address = begin; address < end; address += PGSIZE)
    {
        page_node_t *page = (page_node_t *)address;
        memset(page, 1, PGSIZE);
        page->next = region->list_head.next;
        region->list_head.next = page;
        region->allocable++;
    }
}

static alloc_region_t *select_region(bool in_kernel)
{
    return in_kernel ? &kern_region : &user_region;
}

// 物理内存的初始化
// 本质上就是填写kern_region和user_region, 包括基本数值和空闲链表
void pmem_init(void)
{
    uint64 alloc_begin = (uint64)ALLOC_BEGIN;
    uint64 alloc_end = (uint64)ALLOC_END;
    uint64 kernel_end = alloc_begin + KERN_PAGES * PGSIZE;

    assert(kernel_end >= alloc_begin && kernel_end < alloc_end,
           "pmem_init: invalid kernel region");

    region_init(&kern_region, alloc_begin, kernel_end, "pmem.kernel");
    region_init(&user_region, kernel_end, alloc_end, "pmem.user");
}

// 尝试返回一个可分配的清零后的物理页
// 资源不足返回NULL，由调用者决定恢复或终止启动。
void *pmem_alloc(bool in_kernel)
{
    alloc_region_t *region = select_region(in_kernel);
    page_node_t *page;

    spinlock_acquire(&region->lk);
#if defined(LAB4_TEST) || defined(LAB5_TEST)
    if (fail_after == 0)
    {
        spinlock_release(&region->lk);
        return NULL;
    }
    if (fail_after > 0)
        fail_after--;
#endif
    page = region->list_head.next;
    if (page != NULL)
    {
        region->list_head.next = page->next;
        region->allocable--;
        uint64 index = ((uint64)page - PHYS_MEM_BASE) / PGSIZE;
        assert(!allocated[index], "pmem_alloc: already allocated");
        allocated[index] = 1;
    }
    spinlock_release(&region->lk);

    if (page == NULL)
        return NULL;

    memset(page, 0, PGSIZE);

    return page;
}

// 释放一个物理页
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel)
{
    alloc_region_t *region = select_region(in_kernel);

    assert((page % PGSIZE) == 0, "pmem_free: page is not aligned");
    assert(page >= region->begin && page < region->end,
           "pmem_free: page outside selected region");

    spinlock_acquire(&region->lk);
    uint64 index = (page - PHYS_MEM_BASE) / PGSIZE;
    assert(allocated[index], "pmem_free: double free");
    allocated[index] = 0;
    memset((void *)page, 1, PGSIZE);
    page_node_t *node = (page_node_t *)page;
    node->next = region->list_head.next;
    region->list_head.next = node;
    region->allocable++;
    spinlock_release(&region->lk);
}

bool check_inkernel(uint64 page)
{
    return page >= kern_region.begin && page < kern_region.end;
}

uint32 pmem_available(bool in_kernel)
{
    alloc_region_t *region = select_region(in_kernel);
    spinlock_acquire(&region->lk);
    uint32 count = region->allocable;
    spinlock_release(&region->lk);
    return count;
}
