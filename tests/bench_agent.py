#!/usr/bin/env python3
"""QEMU wall-time experiment, not an instruction-count or physical-hardware claim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import re
import selectors
import shlex
import statistics
import subprocess
import time

import test_lab5 as lab5

MODES = {'baseline': {'AEGIS': 0},
         'enforce': {'AEGIS': 1, 'AEGIS_AUDIT': 0},
         'audit': {'AEGIS': 1, 'AEGIS_AUDIT': 1},
         'filtered': {'AEGIS': 1, 'AEGIS_AUDIT': 1, 'AEGIS_AUDIT_SUCCESS': 0}}
PATTERN = re.compile(rb'aegis bench workload=(query|map-cycle) sample=(\d+) ticks=(\d+) iterations=(\d+)')


def run(target, logfile):
    command = [os.environ.get('QEMU', 'qemu-system-riscv64'), '-machine', 'virt', '-accel', 'tcg,thread=multi',
               '-bios', os.environ.get('OPENSBI', 'default'), '-kernel',
               str(target / 'kernel/kernel-qemu.elf'), '-m', '130M', '-smp', '2', '-nographic']
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = bytearray()
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    try:
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            for key, _ in selector.select(0.1):
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk: raise RuntimeError('QEMU exited before benchmark completion')
                output.extend(chunk)
            if b'panic!' in output or b'BENCH FAILED' in output:
                raise RuntimeError(f'benchmark assertion failed: {logfile}')
            if b'aegis BENCH PASS' in output and b'faulted process cleaned up' in output:
                break
        else:
            raise RuntimeError(f'benchmark timed out: {logfile}')
    finally:
        selector.close()
        if process.poll() is None: process.terminate()
        try:
            remainder, _ = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            remainder, _ = process.communicate()
        output.extend(remainder)
        logfile.write_bytes(output)
    samples = []
    for workload, index, ticks, iterations in PATTERN.findall(output):
        samples.append({'workload': workload.decode(), 'sample': int(index),
                        'ticks': int(ticks), 'iterations': int(iterations)})
    for workload, iterations in [('query', 1024), ('map-cycle', 128)]:
        group = [s for s in samples if s['workload'] == workload]
        assert [s['sample'] for s in group] == list(range(31)), (logfile, workload)
        assert all(s['ticks'] > 0 and s['iterations'] == iterations for s in group)
    assert b'cause=0x000000000000000d' in output and b'stval=0x0000000000000000' in output
    return samples


def source_digest():
    paths = [*sorted((lab5.ROOT / 'src').rglob('*')),
             *[lab5.ROOT / name for name in ['Makefile', 'common.mk', 'kernel.ld', 'user.ld',
                                            'tests/agent_bench.c', 'tests/bench_agent.py']]]
    digest = hashlib.sha256()
    for path in paths:
        if path.is_file():
            digest.update(str(path.relative_to(lab5.ROOT)).encode() + b'\0' + path.read_bytes())
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--output', type=Path, default=lab5.ROOT / 'target/tests/agent-bench')
    args = parser.parse_args()
    if args.repeats < 3: parser.error('at least 3 independent boots per mode are required')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    targets = {name: lab5.build(f'bench-{name}', AGENT_BENCH=1, **options)
               for name, options in MODES.items()}
    # 所有模式的用户程序必须完全一致，否则不是同负载比较。
    users = [(target / 'user/initcode.bin').read_bytes() for target in targets.values()]
    assert all(user == users[0] for user in users)
    records, order = [], []
    rng = random.Random(20261023)
    for repeat in range(args.repeats):
        names = list(MODES)
        rng.shuffle(names)
        for name in names:
            filename = f'{repeat + 1}-{name}.log'
            records.append({'mode': name, 'repeat': repeat + 1, 'log': filename,
                            'samples': run(targets[name], out / filename)})
            order.append(filename)
            print(f'benchmark {repeat + 1}/{args.repeats} {name}: PASS', flush=True)
    summaries = []
    for workload in ['query', 'map-cycle']:
        base = None
        for name in MODES:
            runs = [r for r in records if r['mode'] == name]
            medians = [statistics.median(s['ticks'] / s['iterations']
                       for s in r['samples'] if s['workload'] == workload) for r in runs]
            value = statistics.median(medians)
            if base is None: base = value
            summaries.append({'workload': workload, 'mode': name, 'boot_medians_ticks': medians,
                              'median_boot_median_ticks': value,
                              'min_boot_median_ticks': min(medians),
                              'max_boot_median_ticks': max(medians),
                              'change_vs_baseline_percent': (value / base - 1) * 100})
    qemu = os.environ.get('QEMU', 'qemu-system-riscv64')
    report = {'scope': 'QEMU TCG guest time; descriptive microbenchmark, not physical hardware or real Agent performance',
              'configuration': {'machine': 'virt', 'acceleration': 'tcg,thread=multi', 'memory': '130M', 'harts': 2,
                                'timebase_hz': 10000000, 'icount': False, 'uart_injection': False,
                                'samples_per_workload_per_boot': 31, 'warmup_query': 128,
                                'warmup_map_cycles': 16, 'modes': MODES},
              'environment': {'host': platform.platform(),
                              'qemu': subprocess.check_output([qemu, '--version'], text=True).splitlines()[0],
                              'opensbi': os.environ.get('OPENSBI', 'default'),
                              'compiler': subprocess.check_output(shlex.split((targets['baseline'] / 'build-config').read_text().splitlines()[0]) + ['--version'], text=True).splitlines()[0],
                              'source_sha256': source_digest(),
                              'user_image_sha256': hashlib.sha256(users[0]).hexdigest(),
                              'build_configs': {name: (target / 'build-config').read_text().splitlines()
                                                for name, target in targets.items()},
                              'kernel_sha256': {name: hashlib.sha256((target / 'kernel/kernel-qemu.elf').read_bytes()).hexdigest()
                                                for name, target in targets.items()}},
              'run_order': order, 'summary': summaries, 'runs': records,
              'limitations': ['Host scheduling and QEMU timer noise affect results.',
                              'Samples are batch means; no per-syscall tail-latency inference.',
                              'Map-cycle is one mmap+munmap pair, not one syscall.',
                              'No CPU fairness, real Agent throughput, or VF2 result is measured.',
                              'These successful-operation workloads favor filtering; denial storms are not timed.']}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    lines = ['# Agent protection QEMU microbenchmark', '',
             f'Same user image; {len(MODES)} modes; median of independent boot medians. Guest time ticks at 10 MHz.', '',
             '| Workload | Mode | ticks / operation | boot median range | vs baseline |', '| --- | --- | ---: | ---: | ---: |']
    for s in summaries:
        lines.append(f"| {s['workload']} | {s['mode']} | {s['median_boot_median_ticks']:.3f} | {s['min_boot_median_ticks']:.3f}–{s['max_boot_median_ticks']:.3f} | {s['change_vs_baseline_percent']:+.1f}% |")
    lines += ['', 'query = one brk query; map-cycle = one mmap+munmap pair. Each sample is a batch mean.',
              'TCG and host noise limit interpretation; these are not physical-hardware or real-Agent results.',
              'Raw samples, configuration, source hash and logs are in results.json and the adjacent .log files.', '']
    (out / 'README.md').write_text('\n'.join(lines))
    print(f'Benchmark evidence: {out}', flush=True)


if __name__ == '__main__':
    main()
