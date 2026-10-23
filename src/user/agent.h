#pragma once
#include "sys.h"
#include "agent_abi.h"
#if AGENT_SCHED
static inline long agent_create(const agent_policy_t *policy)
{
    return syscall(SYS_agentctl, AGENT_CTL_CREATE, policy);
}
static inline long agent_spawn(unsigned long group)
{
    return syscall(SYS_agentctl, AGENT_CTL_SPAWN, group);
}
static inline long agent_info(agent_info_t *out)
{
    return syscall(SYS_agentctl, AGENT_CTL_INFO, out, 0);
}
static inline long agent_query(unsigned long group, agent_info_t *out)
{
    return syscall(SYS_agentctl, AGENT_CTL_INFO, out, group);
}
static inline long agent_weight(unsigned long group, unsigned long weight)
{
    return syscall(SYS_agentctl, AGENT_CTL_WEIGHT, group, weight);
}
static inline long agent_remove(unsigned long group)
{
    return syscall(SYS_agentctl, AGENT_CTL_REMOVE, group);
}
#endif
