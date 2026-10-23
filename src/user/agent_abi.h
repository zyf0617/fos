#pragma once

enum { AGENT_CTL_CREATE, AGENT_CTL_SPAWN, AGENT_CTL_INFO,
       AGENT_CTL_WEIGHT, AGENT_CTL_REMOVE };
typedef struct agent_policy {
    unsigned long page_limit, process_limit, weight, syscall_mask;
} agent_policy_t;
typedef struct agent_info {
    unsigned long id, page_limit, pages, process_limit, processes, syscall_mask, weight;
} agent_info_t;
_Static_assert(sizeof(agent_policy_t) == 32, "agent policy ABI");
_Static_assert(sizeof(agent_info_t) == 56, "agent info ABI");
