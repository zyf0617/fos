#!/usr/bin/env python3
"""Real userspace fork/exit/wait in cooperative or timer-preempted configurations."""
import json
import argparse
import subprocess
import test_lab5 as lab5


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preempt", action="store_true")
    args = parser.parse_args()
    preempt = int(args.preempt)
    logs = []
    for aegis, audit, success in [(0, 0, 1), (1, 1, 1), (1, 1, 0), (1, 0, 1)]:
        name = f'process-aegis{aegis}-audit{audit}-success{success}' + ('-rr' if preempt else '')
        target = lab5.build(name, PROC_LIFECYCLE=1, PROC_TEST=1, PROC_PREEMPT=preempt,
                            AEGIS=aegis, AEGIS_AUDIT=audit, AEGIS_AUDIT_SUCCESS=success)
        # 自定义运行允许预期的子故障；检查其cause/stval，并继续等待父进程成功与清理。
        markers = [b'process fork isolation and blocking wait PASS', b'process wait retry PASS',
                   b'process child fault reap PASS', b'process unsupported FP fault PASS',
                   b'process slot exhaustion PASS',
                   b'process actual fork OOM rollback PASS', b'process repeated reuse PASS',
                   b'process orphan survived PASS', b'process USER PASS',
                   b'aegis process lifecycle complete', b'aegis process lifecycle cleaned up',
                   b'ticks = 10']
        if aegis: markers.append(b'aegis group cleanup PASS')
        # 允许已验证的预期子故障，同时要求父进程与最终回收继续完成。
        lab5.run(name, target, markers, expected_user_fault=(13, 0))
        log = (target / f'{name}.log').read_bytes()
        assert b'cause=0x0000000000000002' in log
        assert log.index(b'process USER PASS') < log.index(b'process orphan survived PASS')
        assert log.index(b'aegis process lifecycle cleaned up') < log.index(b'ticks = 10')
        if aegis and audit: assert b'kind=8 detail=0x0000000000000000' in log
        logs.append(str(target.relative_to(lab5.ROOT) / f'{name}.log'))
    for scenario in [31, 32, 33]:
        name = f'process-policy-{scenario}' + ('-rr' if preempt else '')
        target = lab5.build(name, PROC_LIFECYCLE=1, PROC_TEST=1, PROC_PREEMPT=preempt, AEGIS=1, SCENARIO=scenario)
        lab5.run(name, target, [b'process fork policy rejection PASS',
                  b'aegis process lifecycle cleaned up', b'aegis group cleanup PASS', b'ticks = 10'])
        if scenario == 31:
            assert b'kind=4 detail=0x0000000000000008' in (target / f'{name}.log').read_bytes()
        logs.append(str(target.relative_to(lab5.ROOT) / f'{name}.log'))
    # 同目录开关切换还原8调用ABI，不能残留生命周期/故障注入。
    target = lab5.build('process-toggle', PROC_LIFECYCLE=1, PROC_TEST=1)
    assert 'proc_fork' in lab5.symbols(target / 'kernel/kernel-qemu.elf')
    target = lab5.build('process-toggle', PROC_LIFECYCLE=1)
    symbols = lab5.symbols(target / 'kernel/kernel-qemu.elf')
    assert 'proc_fork' in symbols and 'pmem_test_fail_after' not in symbols
    lab5.run('process-production', target, [b'lab-5 demo PASS', b'ticks = 3'])
    target = lab5.build('process-toggle', PROC_LIFECYCLE=0)
    assert 'proc_fork' not in lab5.symbols(target / 'kernel/kernel-qemu.elf')
    lab5.run('process-toggle-baseline', target, [b'lab-5 demo PASS', b'ticks = 3'])
    for options in [['PROC_LIFECYCLE=2'], ['PROC_TEST=1'], ['PROC_TEST=2'],
                    ['PROC_LIFECYCLE=1', 'AGENT_TEST=1', 'AEGIS=1'],
                    ['PROC_LIFECYCLE=1', 'AGENT_BENCH=1'],
                    ['PROC_LIFECYCLE=1', 'LAB5_TEST=1'], ['PROC_LIFECYCLE=1', 'TESTING=1']]:
        result = subprocess.run(['make', '--no-print-directory', 'build', *options], cwd=lab5.ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert result.returncode, options
    report = {'scope': 'single boot-hart lifecycle; no board run', 'preempt': bool(preempt),
              'checks': ['independent user memory', 'blocking/retry wait', 'child fault',
                         'slot exhaustion', 'actual fork OOM rollback', 'PID/slot reuse',
                         'orphan continuation and automatic cleanup', 'four protection modes',
                         'capability/page/member fork rejection', 'same-directory feature toggle'],
              'result': 'PASS', 'logs': logs}
    (lab5.OUT / ('process-rr-summary.json' if preempt else 'process-summary.json')).write_text(json.dumps(report, indent=2) + '\n')
    print(f'Process lifecycle acceptance: PASS (preempt={preempt}; no board run)', flush=True)


if __name__ == '__main__':
    main()
