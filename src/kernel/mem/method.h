#pragma once

/* pmem.c: 物理内存管理逻辑 */

void pmem_init(void);
void *pmem_alloc(bool in_kernel);
void pmem_free(uint64 page, bool in_kernel);

/* kvm.c: 内核态虚拟内存管理 + 页表通用函数 */

pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc);
int vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm);
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit);
void vm_print(pgtbl_t pgtbl);
void kvm_init();
void kvm_inithart();

/* test.c: Lab 2内存管理自检 */

void mem_self_test(void);

bool check_inkernel(uint64 page);
uint32 pmem_available(bool in_kernel);
void vm_freewalk(pgtbl_t pgtbl);
#if defined(LAB4_TEST) || defined(LAB5_TEST)
void pmem_test_fail_after(int successful_allocations);
void lab4_memory_test(void);
void proc_self_test(void);
#endif

/* Lab 5: 用户页与区间管理。所有页表修改由唯一拥有者串行化。 */
void vm_merge_user(pgtbl_t dst, pgtbl_t src);
int uvm_copyin(pgtbl_t root, uint64 dst, uint64 src, uint32 len);
int uvm_copyout(pgtbl_t root, uint64 dst, uint64 src, uint32 len);
int uvm_copyin_str(pgtbl_t root, uint64 dst, uint64 src, uint32 maxlen);
uint64 uvm_heap_grow(pgtbl_t root, uint64 top, uint32 len);
uint64 uvm_heap_ungrow(pgtbl_t root, uint64 top, uint32 len);
int64 uvm_ustack_grow(pgtbl_t root, uint64 old_pages, uint64 fault);
void uvm_destroy_pgtbl(pgtbl_t root);
int uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top,
                   uint64 stack_pages, mmap_region_t *mmap);
void mmap_init(void);
mmap_region_t *mmap_region_alloc(void);
void mmap_region_free(mmap_region_t *region);
uint32 mmap_available(void);
uint64 uvm_mmap_find(mmap_region_t *head, uint64 len,
                     mmap_region_t **previous, mmap_region_t **next);
uint64 uvm_mmap(uint64 begin, uint32 npages, int perm);
int uvm_munmap(uint64 begin, uint32 npages);
void uvm_show_mmaplist(mmap_region_t *head);
#ifdef LAB5_TEST
void lab5_self_test(void);
void lab5_pool_worker(void);
#endif
