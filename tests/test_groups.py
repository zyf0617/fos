#!/usr/bin/env python3
"""Group management and scheduling smoke check; no benchmark."""
import subprocess
import test_lab5 as lab5


def main():
    for audit in [1, 0]:
        name = f'groups-smoke-audit{audit}'
        target = lab5.build(name, AEGIS=1, PROC_LIFECYCLE=1, PROC_PREEMPT=1,
                            AGENT_SCHED=1, GROUP_TEST=1, AEGIS_AUDIT=audit)
        lab5.run(name, target, [b'groups USER PASS', b'aegis group cleanup PASS',
                  b'aegis process lifecycle cleaned up'],
                  continued_after=b'aegis process lifecycle cleaned up')
    for flags in [['AGENT_SCHED=1'], ['GROUP_TEST=1'], ['AGENT_SCHED=2'],
                  ['AEGIS=1', 'PROC_LIFECYCLE=1', 'AGENT_SCHED=1'],
                  ['AEGIS=1', 'PROC_LIFECYCLE=1', 'PROC_PREEMPT=1', 'AGENT_SCHED=1',
                   'GROUP_TEST=1', 'PROC_TEST=1']]:
        result = subprocess.run(['make', '--no-print-directory', 'build', *flags], cwd=lab5.ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert result.returncode, flags
    print('Group management smoke check: PASS', flush=True)


if __name__ == '__main__':
    main()
