#!/usr/bin/env python3
"""Build isolated images and check the actual QEMU serial protocol."""
import argparse
import os
from pathlib import Path
import selectors
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'target' / 'tests'
HELLO = b'proczero: hello world!'
ECHO = b'uart-tesx\x08 \x08t\r\n'


def build(name, **options):
    target = OUT / name
    target.mkdir(parents=True, exist_ok=True)
    command = ['make', '--no-print-directory', '-B', 'build', f'TARGET={target.relative_to(ROOT)}']
    command += [f'{key}={value}' for key, value in options.items()]
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (target / 'build.log').write_bytes(result.stdout)
    if result.returncode:
        raise RuntimeError(f'{name} build failed; see {target / "build.log"}')
    return target


def run(name, target, required, *, hello_count=None, fault=False, context=False, firmware='default'):
    command = [os.environ.get('QEMU', 'qemu-system-riscv64'), '-machine', 'virt',
               '-bios', firmware, '-kernel', str(target / 'kernel/kernel-qemu.elf'),
               '-m', '130M', '-smp', '2', '-nographic']
    process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    output = bytearray()
    injected = False
    deadline = time.monotonic() + 8
    try:
        while time.monotonic() < deadline:
            for key, _ in selector.select(0.05):
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk:
                    raise RuntimeError(f'{name}: QEMU exited before verification')
                output.extend(chunk)
            # 从输出确认执行已开始，避免固定sleep导致串口输入丢失。
            ready = output.count(HELLO) == 2 if hello_count else b'cpu 1 is booting' in output
            if ready and not injected:
                process.stdin.write(b'uart-tesx\x7ft\r')
                process.stdin.flush()
                injected = True
            if b'panic!' in output or b'panic:' in output:
                raise RuntimeError(f'{name}: kernel panic')
            if all(marker in output for marker in required) and ECHO in output:
                break
        else:
            missing = [marker.decode(errors='replace') for marker in required if marker not in output]
            raise RuntimeError(f'{name}: missing {missing}; echo={ECHO in output}')
        if hello_count is not None and output.count(HELLO) != hello_count:
            raise RuntimeError(f'{name}: wrong hello count {output.count(HELLO)}')
        if not fault and b'lab-4 user fault:' in output:
            raise RuntimeError(f'{name}: unexpected user fault')
        if fault:
            if b'cause=0x000000000000000d' not in output or b'stval=0x0000000000000000' not in output:
                raise RuntimeError(f'{name}: wrong fault cause/address')
            if output.find(b'ticks = 10') < output.find(b'faulted process cleaned up'):
                raise RuntimeError(f'{name}: no continued kernel ticks after cleanup')
        if context:
            checked = output.find(b'lab-4 user checks passed')
            if checked < output.find(ECHO) or b'lab-4 user timer interrupt test passed' not in output or checked < output.find(b'lab-4 user timer interrupt test passed'):
                raise RuntimeError(f'{name}: context probe did not span both user interrupts')
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--lab3', action='store_true')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    base = [b'cpu 0 is booting with OpenSBI!', b'cpu 1 is booting with OpenSBI!',
            b'lab-2 memory self-test passed', b'lab-3 timer interrupt test passed']
    if args.lab3:
        target = build('lab3-baseline', BOOT_USER=0)
        run('lab3-baseline', target, base)
        return
    target = build('default')
    run('default', target, base, hello_count=2)
    for name, scenario in [('user-context', 0), ('unknown-syscall', 1), ('zero-page', 2)]:
        target = build(name, TESTING=1, SCENARIO=scenario)
        markers = base + [b'lab-4 memory failure tests passed', b'lab-4 process rollback test passed',
                          b'lab-4 user checks passed', b'lab-4 user UART interrupt handled']
        if scenario == 2:
            markers += [b'lab-4 faulted process cleaned up', b'ticks = 10']
        else:
            markers += [b'lab-4 user timer interrupt test passed']
        run(name, target, markers, hello_count=2, fault=scenario == 2, context=True)
    print(f'Lab 4 acceptance passed; logs: {OUT}', flush=True)


if __name__ == '__main__':
    main()
