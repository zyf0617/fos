#pragma once
void syscall(void);
uint64 arg_raw(int index);
void arg_uint32(int index, uint32 *value);
void arg_uint64(int index, uint64 *value);
int arg_str(int index, char *buffer, int maxlen);
uint64 sys_helloworld(void);
uint64 sys_copyin(void);
uint64 sys_copyout(void);
uint64 sys_copyinstr(void);
uint64 sys_brk(void);
uint64 sys_mmap(void);
uint64 sys_munmap(void);
uint64 sys_printf(void);
