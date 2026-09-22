#include "mod.h"

// 内核空间和用户空间的可分配物理页分开描述
static alloc_region_t kern_region, user_region;

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

    assert(kernel_end >= alloc_begin && kernel_end <= alloc_end,
           "pmem_init: invalid kernel region");

    region_init(&kern_region, alloc_begin, kernel_end, "pmem.kernel");
    region_init(&user_region, kernel_end, alloc_end, "pmem.user");
}

// 尝试返回一个可分配的清零后的物理页
// 失败则panic锁死
void *pmem_alloc(bool in_kernel)
{
    alloc_region_t *region = select_region(in_kernel);
    page_node_t *page;

    spinlock_acquire(&region->lk);
    page = region->list_head.next;
    if (page != NULL)
    {
        region->list_head.next = page->next;
        region->allocable--;
    }
    spinlock_release(&region->lk);

    if (page == NULL)
        panic(in_kernel ? "pmem_alloc: kernel pages exhausted"
                        : "pmem_alloc: user pages exhausted");

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
    page_node_t *node = (page_node_t *)page;
    node->next = region->list_head.next;
    region->list_head.next = node;
    region->allocable++;
    spinlock_release(&region->lk);
}
