#!/usr/bin/env python3
"""Lab 5 builds, resource/permission probes, real user traps, and image checks."""
import argparse
import os
from pathlib import Path
import selectors
import shutil
import struct
import sys
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'target' / 'tests' / 'lab5'
GROUPS = {'copy': 10, 'heap-stack': 11, 'mmap': 12, 'pagetable': 13, 'all': 14}


def build(name, **options):
    target = OUT / name
    target.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(['make', '--no-print-directory', 'build',
                             f'TARGET={target.relative_to(ROOT)}'] +
                            [f'{key}={value}' for key, value in options.items()],
                            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (target / 'build.log').write_bytes(result.stdout)
    if result.returncode:
        raise RuntimeError(f'{name}: build failed; see {target / "build.log"}')
    return target


def run(name, target, markers, fault=None):
    command = [os.environ.get('QEMU', 'qemu-system-riscv64'), '-machine', 'virt',
               '-bios', os.environ.get('OPENSBI', 'default'), '-kernel',
               str(target / 'kernel/kernel-qemu.elf'), '-m', '130M', '-smp', '2', '-nographic']
    process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = bytearray()
    injected = False
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    deadline = time.monotonic() + 12
    try:
        while time.monotonic() < deadline:
            for key, _ in selector.select(0.05):
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk:
                    raise RuntimeError(f'{name}: QEMU exited early')
                output.extend(chunk)
            if b'panic!' in output or b'USER FAILED' in output:
                raise RuntimeError(f'{name}: kernel or user assertion failed')
            if b'cpu 0 is booting' in output and b'cpu 1 is booting' in output and not injected:
                process.stdin.write(b'lab5-uart\r')
                process.stdin.flush()
                injected = True
            if all(marker in output for marker in markers) and b'lab5-uart\r\n' in output:
                break
        else:
            raise RuntimeError(f'{name}: missing {[m for m in markers if m not in output]}')
        if fault:
            cause, stval = fault
            if f'cause=0x{cause:016x}'.encode() not in output or f'stval=0x{stval:016x}'.encode() not in output:
                raise RuntimeError(f'{name}: wrong fault')
            if output.find(b'ticks = 10') < output.find(b'faulted process cleaned up'):
                raise RuntimeError(f'{name}: kernel did not continue after cleanup')
        elif b'user fault:' in output:
            raise RuntimeError(f'{name}: unexpected user fault')
        print(f'{name}: PASS', flush=True)
    finally:
        selector.close()
        if process.poll() is None:
            process.terminate()
        try:
            remainder, _ = process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            remainder, _ = process.communicate()
        output.extend(remainder)
        (target / f'{name}.log').write_bytes(output)


def symbols(elf):
    nm = next((shutil.which(p + 'nm') for p in ['riscv64-linux-gnu-', 'riscv64-unknown-elf-',
                                               'riscv64-elf-', 'riscv64-none-elf-'] if shutil.which(p + 'nm')), None)
    if not nm:
        raise RuntimeError('RISC-V nm is required for image layout verification')
    lines = subprocess.check_output([nm, '-n', str(elf)], text=True).splitlines()
    return {parts[2]: int(parts[0], 16) for line in lines if len(parts := line.split()) == 3}


def native_platform_test():
    if not sys.platform.startswith('linux'):
        print('platform MMIO: SKIP (Linux host required; run on Ubuntu)', flush=True)
        return
    target = OUT / 'native-platform'
    target.mkdir(parents=True, exist_ok=True)
    for platform in ['qemu', 'vf2']:
        flags = ['-DVISIONFIVE2'] if platform == 'vf2' else []
        command = [os.environ.get('HOST_CC', 'cc'), '-O2', '-Wall', '-Werror', '-fno-builtin', *flags]
        for name, sources in [('mmio', ['tests/platform_mmio.c', 'src/kernel/lib/uart.c', 'src/kernel/trap/plic.c']),
                              ('constants', ['tests/platform_constants.c'])]:
            executable = target / f'{platform}-{name}'
            result = subprocess.run([*command, *sources, '-o', str(executable)], cwd=ROOT,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (target / f'{platform}-{name}-build.log').write_bytes(result.stdout)
            if result.returncode:
                raise RuntimeError(f'{platform} {name}: host probe build failed')
            result = subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)
            (target / f'{platform}-{name}.log').write_bytes(result.stdout)
            if result.returncode:
                raise RuntimeError(f'{platform} {name}: host probe failed: {result.stdout!r}')
    print('platform MMIO: PASS (actual UART/PLIC sources and constants; QEMU + VF2)', flush=True)


def platform_test():
    native_platform_test()
    qemu = build('platform-qemu', PLATFORM='qemu')
    image = qemu / 'kernel/kernel-qemu.elf'
    before = {p.relative_to(qemu): p.stat().st_mtime_ns for p in qemu.rglob('*')
              if p.suffix in {'.o', '.elf', '.bin', '.h', '.ld'}}
    vf2 = build('platform-vf2', PLATFORM='vf2')
    build('platform-qemu', PLATFORM='qemu')
    after = {p.relative_to(qemu): p.stat().st_mtime_ns for p in qemu.rglob('*')
             if p.suffix in {'.o', '.elf', '.bin', '.h', '.ld'}}
    assert before == after, 'platform switch rebuilt unchanged qemu artifacts'
    qsym = symbols(image)
    assert qsym['_entry'] == 0x80200000 and qsym['ALLOC_END'] == 0x88200000
    velf = vf2 / 'kernel/kernel-vf2.elf'
    vsym = symbols(velf)
    data = (vf2 / 'kernel/kernel-vf2.bin').read_bytes()
    assert vsym['_entry'] == 0x40200000 and vsym['ALLOC_END'] == 0x48200000
    assert struct.unpack_from('<Q', velf.read_bytes(), 24)[0] == vsym['_entry']
    code0, code1, offset, size, flags, version, res1, res2, magic, magic2, res3 = struct.unpack_from('<IIQQQIIQQII', data)
    assert code0 & 0x7f == 0x6f and code1 == 0x13 and offset == 0x200000
    immediate = ((code0 >> 31) << 20) | (((code0 >> 12) & 255) << 12) | (((code0 >> 20) & 1) << 11) | (((code0 >> 21) & 1023) << 1)
    assert immediate == 64 and (code0 >> 7) & 31 == 0, 'Image entry does not jump past header'
    assert size == vsym['KERNEL_IMAGE_SIZE'] == vsym['ALLOC_BEGIN'] - vsym['_entry']
    assert size and size % 4096 == 0 and len(data) < size < 128 * 1024 * 1024
    assert (flags, version, res1, res2, magic, magic2, res3) == (0, 2, 0, 0, 0x5643534952, 0x05435352, 0)
    assert vsym['BSS_BEGIN'] % 8 == 0 and vsym['BSS_END'] % 8 == 0
    assert vsym['BSS_BEGIN'] <= vsym['CPU_stack'] < vsym['BSS_END']
    for arguments in [['build', 'PLATFORM=bad'], ['build', 'PLATFORM='], ['build', 'PLATFORM=qemu vf2'], ['build', 'CPUNUM=1'],
                      ['run', 'PLATFORM=vf2'], ['debug', 'PLATFORM=vf2']]:
        result = subprocess.run(['make', '--no-print-directory'] + arguments, cwd=ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert result.returncode != 0, arguments
        if 'PLATFORM=vf2' in arguments:
            assert b'use U-Boot booti' in result.stdout
    print('platform: PASS (build isolation, no-op, Image header, BSS, rejected options; no board run)', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--group', choices=[*GROUPS, 'platform'], default='all')
    args = parser.parse_args()
    if args.group in {'all', 'platform'}:
        platform_test()
    if args.group == 'platform':
        return
    group = args.group
    name = f'lab5-{group}'
    target = build(name, LAB5_TEST=1, SCENARIO=GROUPS[group])
    base = [b'lab-5 kernel suite PASS', b'lab-5 USER PASS',
            b'lab-4 faulted process cleaned up', b'ticks = 10']
    if group == 'all':
        base += [b'kernel copy PASS', b'kernel heap-stack PASS', b'kernel mmap PASS',
                 b'kernel pagetable PASS', b'kernel pool concurrent PASS']
    else:
        base += [f'kernel {group} PASS'.encode()]
    run(name, target, base, fault=(13, 0))
    if group == 'all':
        for name, scenario, cause, address in [('code-write', 21, 15, 0x1000),
                                               ('trapframe-read', 22, 13, (1 << 38) - 8192),
                                               ('kernel-jump', 23, 12, 0x80200000)]:
            target = build(name, LAB5_TEST=1, SCENARIO=scenario)
            run(name, target, [b'lab-5 USER PASS', b'faulted process cleaned up', b'ticks = 10'],
                fault=(cause, address))
        target = build('default')
        run('default', target, [b'lab-5 demo PASS', b'lab-3 timer interrupt test passed'])
    print(f'Lab 5 QEMU acceptance passed; logs: {OUT}', flush=True)


if __name__ == '__main__':
    main()
