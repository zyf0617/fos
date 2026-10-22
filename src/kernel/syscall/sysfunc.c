#include "mod.h"
#define STRING_LIMIT 128
#define ARRAY_LIMIT 64

uint64 sys_helloworld(void)
{
    printf("proczero: hello world!\n");
    return 0;
}
uint64 sys_copyin(void)
{
    uint64 count = arg_raw(1);
    if (count > ARRAY_LIMIT) return (uint64)-1;
    int data[ARRAY_LIMIT];
    if (uvm_copyin(myproc()->pgtbl, (uint64)data, arg_raw(0), count * sizeof(int)))
        return (uint64)-1;
    for (uint32 i = 0; i < count; i++) printf("copyin[%d]=%d\n", i, data[i]);
    return 0;
}
uint64 sys_copyout(void)
{
    const int data[] = {1, 2, 3, 4, 5};
    return uvm_copyout(myproc()->pgtbl, arg_raw(0), (uint64)data, sizeof(data)) ? (uint64)-1 : 5;
}
uint64 sys_copyinstr(void)
{
    char text[STRING_LIMIT];
    if (arg_str(0, text, sizeof(text))) return (uint64)-1;
    printf("%s\n", text);
    return 0;
}
uint64 sys_printf(void)
{
    char text[STRING_LIMIT];
    if (arg_str(0, text, sizeof(text))) return (uint64)-1;
    uint64 count = 0;
    while (text[count]) count++;
    printf("%s", text);
    return count;
}
uint64 sys_brk(void)
{
    proc_t *p = myproc();
    uint64 next = arg_raw(0), old = p->heap_top;
    if (!next) return old;
    if (next < USER_HEAP_BASE || next > MMAP_BEGIN) return (uint64)-1;
    uint64 delta = next > old ? next - old : old - next;
    if (delta > 0xfffffffful) return (uint64)-1;
    uint64 result = next >= old ? uvm_heap_grow(p->pgtbl, old, delta)
                                : uvm_heap_ungrow(p->pgtbl, old, delta);
    if (!result) return (uint64)-1;
    p->heap_top = result;
    return result;
}
static bool page_length(uint64 length)
{
    return length && length <= 0xfffffffful && length % PGSIZE == 0;
}
uint64 sys_mmap(void)
{
    uint64 length = arg_raw(1);
    if (!page_length(length)) return 0;
    return uvm_mmap(arg_raw(0), length / PGSIZE, PTE_R | PTE_W);
}
uint64 sys_munmap(void)
{
    uint64 length = arg_raw(1);
    if (!page_length(length)) return (uint64)-1;
    return uvm_munmap(arg_raw(0), length / PGSIZE);
}
