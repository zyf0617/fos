#include "mod.h"

typedef struct region_node {
    mmap_region_t region;
    struct region_node *next;
    bool allocated;
} region_node_t;
static region_node_t nodes[N_MMAP], *free_nodes;
static spinlock_t pool_lock;
static uint32 free_count;

void mmap_init(void)
{
    spinlock_init(&pool_lock, "mmap.pool");
    free_nodes = NULL;
    free_count = N_MMAP;
    for (int i = N_MMAP - 1; i >= 0; i--)
    {
        nodes[i].allocated = false;
        nodes[i].next = free_nodes;
        free_nodes = &nodes[i];
    }
}

mmap_region_t *mmap_region_alloc(void)
{
    spinlock_acquire(&pool_lock);
    region_node_t *node = free_nodes;
    if (node)
    {
        free_nodes = node->next;
        node->allocated = true;
        free_count--;
        memset(&node->region, 0, sizeof(node->region));
    }
    spinlock_release(&pool_lock);
    return node ? &node->region : NULL;
}

void mmap_region_free(mmap_region_t *region)
{
    uint64 address = (uint64)region, base = (uint64)nodes;
    assert(address >= base && address < base + sizeof(nodes) &&
           (address - base) % sizeof(region_node_t) == 0,
           "mmap_region_free: foreign node");
    region_node_t *node = (region_node_t *)region;
    spinlock_acquire(&pool_lock);
    assert(node->allocated, "mmap_region_free: double free");
    node->allocated = false;
    node->next = free_nodes;
    free_nodes = node;
    free_count++;
    spinlock_release(&pool_lock);
}

uint32 mmap_available(void)
{
    spinlock_acquire(&pool_lock);
    uint32 count = free_count;
    spinlock_release(&pool_lock);
    return count;
}
