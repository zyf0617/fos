#!/usr/bin/env python3
"""Timer-preempted RR acceptance with the same CPU-only image as a serial control."""
import hashlib
import json
import re
import subprocess
import test_lab5 as lab5


def main():
    images, results = set(), []
    modes = [(0, 0, 0, 1), (1, 0, 0, 1), (1, 1, 1, 1), (1, 1, 1, 0), (1, 1, 0, 1)]
    for preempt, aegis, audit, success in modes:
        name = f'rr-preempt{preempt}-aegis{aegis}-audit{audit}-success{success}'
        target = lab5.build(name, PROC_LIFECYCLE=1, PROC_PREEMPT=preempt, RR_TEST=1,
                            AEGIS=aegis, AEGIS_AUDIT=audit, AEGIS_AUDIT_SUCCESS=success)
        user_hash = hashlib.sha256((target / 'user/initcode.bin').read_bytes()).hexdigest()
        images.add(user_hash)
        markers = [b'rr START A', b'rr START B', b'rr DONE A', b'rr DONE B', b'rr USER PASS',
                   b'aegis process lifecycle cleaned up']
        if aegis: markers.append(b'aegis group cleanup PASS')
        lab5.run(name, target, markers, continued_after=b'aegis process lifecycle cleaned up')
        log_path = target / f'{name}.log'
        log = log_path.read_bytes()
        overlap = max(log.index(b'rr START A'), log.index(b'rr START B')) < min(
            log.index(b'rr DONE A'), log.index(b'rr DONE B'))
        samples = [dict(zip(['pid', 'dispatches', 'ticks', 'preemptions'], map(int, row)))
                   for row in re.findall(rb'aegis rr pid=(\d+) dispatches=(\d+) ticks=(\d+) preemptions=(\d+)', log)]
        if preempt:
            assert overlap, 'CPU tasks ran serially despite timer preemption'
            assert {row['pid'] for row in samples} == {0, 1, 2}
            for row in samples:
                if row['pid']:
                    assert row['preemptions'] >= 2 and row['dispatches'] > row['preemptions']
                    assert row['ticks'] >= row['preemptions']
        else:
            assert not overlap and not samples, 'negative control did not run serially'
        results.append({'name': name, 'preempt': bool(preempt), 'aegis': aegis, 'audit': audit,
                        'audit_success': success, 'both_started_before_either_completed': overlap,
                        'process_samples': samples, 'user_sha256': user_hash,
                        'kernel_sha256': hashlib.sha256((target / 'kernel/kernel-qemu.elf').read_bytes()).hexdigest(),
                        'log': str(log_path.relative_to(lab5.ROOT))})
    assert len(images) == 1, 'control and RR did not use the same user image'
    target = lab5.build('rr-toggle', PROC_LIFECYCLE=1, PROC_PREEMPT=1)
    assert 'proc_timer_tick' in lab5.symbols(target / 'kernel/kernel-qemu.elf')
    target = lab5.build('rr-toggle', PROC_LIFECYCLE=1, PROC_PREEMPT=0)
    assert 'proc_timer_tick' not in lab5.symbols(target / 'kernel/kernel-qemu.elf')
    lab5.run('rr-toggle-cooperative', target, [b'lab-5 demo PASS', b'ticks = 3'])
    for options in [['PROC_PREEMPT=1'], ['PROC_PREEMPT=2'], ['PROC_PREEMPT='], ['RR_TEST=1'],
                    ['RR_TEST=2'], ['PROC_LIFECYCLE=1', 'RR_TEST=1', 'PROC_TEST=1']]:
        result = subprocess.run(['make', '--no-print-directory', 'build', *options], cwd=lab5.ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert result.returncode, options
    tracked = sorted(lab5.ROOT.glob('src/**/*'))
    tracked += [lab5.ROOT / p for p in ['Makefile', 'common.mk', 'kernel.ld', 'user.ld',
                'tests/rr_user.c', 'tests/rr_context.S', 'tests/test_rr.py', 'tests/test_lab5.py']]
    digest = hashlib.sha256()
    for p in tracked:
        if p.is_file():
            digest.update(str(p.relative_to(lab5.ROOT)).encode() + b'\0' + p.read_bytes() + b'\0')
    report = {'scope': 'QEMU boot-hart user-mode timer preemption; no kernel preemption/SMP/fairness benchmark',
              'result': 'PASS', 'source_sha256': digest.hexdigest(),
              'negative_control': 'serial execution confirmed', 'identical_user_image': True,
              'results': results}
    (lab5.OUT / 'rr-summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print('RR acceptance: PASS (CPU-only overlap, register patterns, serial control)', flush=True)


if __name__ == '__main__':
    main()
