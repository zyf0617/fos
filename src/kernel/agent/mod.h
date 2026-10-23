#pragma once
#include "../proc/mod.h"

#define AGENT_AUDIT_CAPACITY 64
#define AGENT_SYSCALL_MASK 255ul
enum { AGENT_ATTACH = 1, AGENT_DETACH, AGENT_SYSCALL,
       AGENT_CAP_DENY, AGENT_QUOTA_DENY, AGENT_FAULT };

typedef struct agent_group {
    spinlock_t lock;
    uint64 id, page_limit, pages, process_limit, processes, syscall_mask;
    uint64 peak_pages, quota_denied, capability_denied;
} agent_group_t;

typedef struct agent_stats {
    uint64 id, page_limit, pages, peak_pages, process_limit, processes;
    uint64 syscall_mask, quota_denied, capability_denied;
} agent_stats_t;

typedef struct agent_event {
    uint64 sequence, time, group, pid, kind, detail, result, pages;
} agent_event_t;

void agent_init(void);
void agent_group_init(agent_group_t *g, uint64 id, uint64 pages,
                      uint64 processes, uint64 syscall_mask);
int agent_attach(proc_t *p, agent_group_t *g, uint64 initial_pages);
int agent_attach_boot(proc_t *p);
void agent_boot_check(void);
void agent_detach(proc_t *p);
void agent_snapshot(agent_group_t *g, agent_stats_t *out);
int agent_restrict(agent_group_t *g, uint64 pages, uint64 processes, uint64 syscall_mask);
int agent_charge(pgtbl_t root, uint64 pages);
void agent_uncharge(pgtbl_t root, uint64 pages);
bool agent_allowed(proc_t *p, uint64 number);
void agent_record(proc_t *p, uint64 kind, uint64 detail, uint64 result);
uint32 agent_audit_read(uint64 after, agent_event_t *out, uint32 capacity, uint64 *lost);
void agent_audit_dump(void);
#ifdef AGENT_TEST
void agent_self_test(void);
void agent_test_worker(void);
#endif
