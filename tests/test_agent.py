#!/usr/bin/env python3
"""Actual group/quota/capability hooks plus ordinary Lab 5 compatibility."""
import test_lab5 as lab5
import json
import subprocess


def main():
    target = lab5.build('aegis-protection', AEGIS=1, AGENT_TEST=1, SCENARIO=30)
    lab5.run('aegis-protection', target, [
        b'aegis kernel protection PASS', b'aegis capability deny PASS',
        b'aegis quota reuse PASS', b'aegis USER PASS', b'lab-5 stack growth failed',
        b'aegis group cleanup PASS', b'aegis audit seq=', b'ticks = 10'])
    log = (target / 'aegis-protection.log').read_bytes()
    assert b'aegis USER FAILED' not in log
    assert b'kind=4 detail=0x0000000000000002 result=0xffffffffffffffff' in log
    assert b'kind=5' in log and b'kind=2 detail=0x0000000000000000 result=0x0000000000000000 pages=0' in log
    assert log.index(b'aegis USER PASS') < log.index(b'lab-5 stack growth failed') < log.index(b'aegis group cleanup PASS') < log.index(b'ticks = 10')
    assert b'kind=6 detail=0x000000000000000f' in log
    # 开启机制但保留完整权限和足够预算，运行原有Lab5真实用户负载。
    target = lab5.build('aegis-lab5', AEGIS=1, LAB5_TEST=1, SCENARIO=14)
    lab5.run('aegis-lab5', target, [b'lab-5 kernel suite PASS', b'lab-5 USER PASS',
             b'faulted process cleaned up', b'aegis group cleanup PASS', b'ticks = 10'], fault=(13, 0))
    # 同一输出目录切换开关，不能残留安全策略或测试入口。
    target = lab5.build('aegis-toggle', AEGIS=1)
    assert 'agent_charge' in lab5.symbols(target / 'kernel/kernel-qemu.elf')
    target = lab5.build('aegis-toggle', AEGIS=0)
    assert 'agent_charge' not in lab5.symbols(target / 'kernel/kernel-qemu.elf')
    lab5.run('aegis-baseline', target, [b'lab-5 demo PASS', b'lab-3 timer interrupt test passed'])
    target = lab5.build('aegis-toggle', AEGIS=1)
    assert 'agent_charge' in lab5.symbols(target / 'kernel/kernel-qemu.elf')
    lab5.run('aegis-default', target, [b'lab-5 demo PASS', b'lab-3 timer interrupt test passed'])
    for option in ['AEGIS=2', 'AEGIS=', 'AEGIS=0 1', 'AGENT_TEST=1']:
        result = subprocess.run(['make', '--no-print-directory', 'build', option], cwd=lab5.ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert result.returncode != 0, option
    report = {
        'scope': 'QEMU protection prototype; no real LLM, scheduler, or board run',
        'checks': {'group_shared_budget_and_process_limit': 'PASS',
                   'heap_mmap_stack_quota_and_oom_rollback': 'PASS',
                   'actual_ecall_permission_denial_and_quota_reuse': 'PASS',
                   'audit_order_overwrite_and_loss': 'PASS',
                   'fault_cleanup_and_continued_ticks': 'PASS',
                   'aegis_enabled_lab5_compatibility': 'PASS',
                   'same_directory_toggle_and_baseline': 'PASS',
                   'rejected_build_options': 'PASS'},
        'performance_overhead': 'NOT_MEASURED',
        'logs': ['aegis-protection/aegis-protection.log', 'aegis-lab5/aegis-lab5.log',
                 'aegis-toggle/aegis-baseline.log', 'aegis-toggle/aegis-default.log']}
    (lab5.OUT / 'aegis-summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Aegis protection acceptance: PASS (prototype; no scheduler/LLM/board claim)', flush=True)


if __name__ == '__main__':
    main()
