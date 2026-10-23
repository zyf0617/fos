#pragma once

// proc.c: 资源所有权、复制及第一个进程初始化

pgtbl_t proc_pgtbl_init(uint64 trapframe);
void proc_make_first();

void proc_return(void) __attribute__((noreturn));
#ifdef LAB4_TEST
void proc_self_test(void);
#endif

// 仅准备资源；调用者独占src/dst，dst全零。成功后仍须设置内核栈/上下文。
int proc_clone_resources(proc_t *dst, const proc_t *src, int pid);
// 仅释放已停止、未绑定cpu.proc的资源；内核栈由运行层持有。
void proc_release_resources(proc_t *p);
#ifdef LAB5_TEST
void proc_clone_self_test(void);
#endif

#if PROC_LIFECYCLE
void proc_lifecycle_run(proc_t *initial);
uint64 proc_fork(void);
uint64 proc_wait(uint64 status);
void proc_yield(void);
void proc_exit(uint64 status) __attribute__((noreturn));
#endif
