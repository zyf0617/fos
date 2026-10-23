#include "../src/user/agent.h"
extern long rr_context(unsigned long duration, unsigned long seed);
static void check(int ok)
{
    if (!ok)
    {
        syscall(SYS_printf, "groups USER FAILED\n");
        syscall(SYS_exit, -99);
        for (;;) ;
    }
}
static void finish(long code)
{
    syscall(SYS_exit, code);
    for (;;) ;
}
__attribute__((section(".text.entry"))) int main(void)
{
    agent_info_t info;
    agent_policy_t a = {16, 4, 1, (1ul << 14) - 1};
    agent_policy_t b = {2, 1, 2, (1ul << 14) - 1};
    check(agent_info(&info) == 0 && info.id == 1 && info.weight == 1);
    check(agent_create((void *)1) == -1 && agent_info((void *)1) == -1);
    a.weight = 0; check(agent_create(&a) == -1);
    a.weight = 17; check(agent_create(&a) == -1);
    a.weight = 1; a.syscall_mask |= 1ul << 63; check(agent_create(&a) == -1);
    a.syscall_mask = (1ul << 14) - 1;
    long ga = agent_create(&a), gb = agent_create(&b);
    check(ga > 1 && gb > ga && agent_remove(1) == -1);
    check(agent_weight(ga, 0) == -1 && agent_weight(ga, 17) == -1 && agent_weight(ga, 3) == 0);
    check(agent_spawn(999) == -1 && syscall(SYS_agentctl, 0x100000000ul, ga) == -1);
    agent_policy_t tiny = {1, 1, 1, (1ul << 14) - 1};
    long small = agent_create(&tiny);
    check(small > gb && agent_spawn(small) == -1 && agent_remove(small) == 0);
    check(agent_info(&info) == 0 && info.pages == 2 && info.processes == 1);
    check(agent_spawn((1ul << 32) + ga) == -1 && agent_weight(ga, (1ul << 32) + 1) == -1);
    check(agent_query(ga, &info) == 0 && info.weight == 3 && !info.processes);
    long pa = agent_spawn(ga);
    check(pa >= 0);
    if (!pa)
    {
        check(agent_info(&info) == 0 && info.id == (unsigned long)ga && info.weight == 3);
        check(agent_query(ga, &info) == 0 && agent_query(gb, &info) == -1);
        check(agent_create(&a) == -1 && agent_weight(ga, 2) == -1 &&
              agent_spawn(gb) == -1 && agent_remove(ga) == -1);
        check(rr_context(2000000ul, 256) == 0);
        long child = syscall(SYS_fork);
        check(child >= 0);
        if (!child)
        {
            check(agent_info(&info) == 0 && info.id == (unsigned long)ga && agent_create(&a) == -1);
            finish(17);
        }
        long status;
        check(syscall(SYS_wait, &status) == child && status == 17);
        syscall(SYS_yield);
        finish(21);
    }
    check(agent_remove(ga) == -1);
    long pb = agent_spawn(gb);
    check(pb >= 0);
    if (!pb)
    {
        check(agent_info(&info) == 0 && info.id == (unsigned long)gb && info.weight == 2 && info.pages == 2);
        check(rr_context(2000000ul, 512) == 0);
        check(syscall(SYS_brk, 3 * 4096) == -1 && syscall(SYS_mmap, 0, 4096) == 0);
        check(syscall(SYS_fork) == -1);
        finish(22);
    }
    long seen = 0, status;
    for (int i = 0; i < 2; i++)
    {
        long pid = syscall(SYS_wait, &status);
        if (pid == pa) { check(status == 21 && !(seen & 1)); seen |= 1; }
        else { check(pid == pb && status == 22 && !(seen & 2)); seen |= 2; }
    }
    check(seen == 3 && agent_remove(ga) == 0 && agent_remove(gb) == 0 &&
          agent_query(ga, &info) == -1 && agent_remove(ga) == -1);
    long ids[3];
    for (int i = 0; i < 3; i++) { ids[i] = agent_create(&a); check(ids[i] > gb); }
    check(agent_create(&a) == -1 && agent_spawn(ga) == -1);
    for (int i = 0; i < 3; i++) check(agent_remove(ids[i]) == 0);
    syscall(SYS_printf, "groups USER PASS\n");
    finish(0);
}
