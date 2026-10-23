#pragma once
#define SYS_helloworld 0
#define SYS_copyin 1
#define SYS_copyout 2
#define SYS_copyinstr 3
#define SYS_brk 4
#define SYS_mmap 5
#define SYS_munmap 6
#define SYS_printf 7
#if PROC_LIFECYCLE
#define SYS_fork 8
#define SYS_exit 9
#define SYS_wait 10
#define SYS_yield 11
#define SYS_getpid 12
#define SYS_MAX_NUM 12
#else
#define SYS_MAX_NUM 7
#endif
