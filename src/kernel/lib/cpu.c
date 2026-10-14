#include "mod.h"

static cpu_t cpus[NCPU];

cpu_t *mycpu(void)
{
    uint64 hartid = r_tp();
    assert(hartid < NCPU, "mycpu: invalid hart id");
    return &cpus[hartid];
}

int mycpuid(void)
{
    int hartid = r_tp();
    return hartid;
}

proc_t *myproc(void)
{
    return mycpu()->proc;
}
