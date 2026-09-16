#!/usr/bin/env python3
"""Real-thread production log queue tests; mocked OS I/O, no device access."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CASES = ['disabled', 'cold', *['startup' + str(i) for i in range(1, 7)],
         'fifo', 'capacity', 'critical', 'ownerflush', 'errorfull', 'barrier',
         'self', 'join', 'hints', 'ownercritical', 'openflush', 'console', 'syncerror', 'partial', 'zero', 'impossible',
         'negative', 'consoleerror', 'update-write', 'update-console',
         *[f'status-console-{status}-{mode}' for status in (0, 1, 2048) for mode in ('off', 'on')],
         'deadline-wrap', 'simultaneous-flushers',
         'toggle-cycle', 'toggle-startup', 'toggle-open', 'toggle-ordinary', 'toggle-ordinary-timeout',
         'toggle-sync-error', 'toggle-error', 'toggle-barrier', 'toggle-blocked-file',
         'toggle-blocked-console', 'toggle-deadline']


def run(out, modes):
    sdk = Path(os.environ.get('VITASDK', Path.home() / 'vitasdk'))
    source = (ROOT / 'runtime/main.c').read_text()
    begin = source.index('static void xv_gfx_finish(void)')
    end = source.index('static void __attribute__((unused)) xv_gfx_shutdown', begin)
    (out / 'log_exit.inc').write_text(source[begin:end])
    for mode in modes:
        exe = out / ('log-async-' + mode)
        flags = []
        if mode == 'asan':
            flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        if mode == 'tsan':
            flags = ['-fsanitize=thread', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                        '-Werror', '-Wno-misleading-indentation', '-DXV_PROFILE_ASYNC_REPORT',
                        '-DTEST_EXIT_PROTOCOL', '-I' + str(out),
                        '-idirafter', str(sdk / 'arm-vita-eabi/include'),
                        str(ROOT / 'tools/tests/log_async.c'), '-pthread', *flags, '-o', str(exe)], check=True)
        env = {**os.environ, 'ASAN_OPTIONS': 'abort_on_error=1:detect_leaks=1',
               'TSAN_OPTIONS': 'halt_on_error=1'}
        for case in CASES:
            result = subprocess.run([str(exe), case], env=env, text=True, capture_output=True, timeout=15)
            print(mode, result.stdout.strip(), flush=True)
            if result.returncode:
                raise RuntimeError(f'{mode}/{case}: {result.returncode}\n{result.stdout}\n{result.stderr}')
    print('PASS: production native writer, exact FIFO/lifetime/backpressure, partial/error retry, barriers and shutdown')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--mode', choices=['normal', 'asan', 'tsan', 'all'], default='all')
    args = parser.parse_args()
    modes = ['normal', 'asan', 'tsan'] if args.mode == 'all' else [args.mode]
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        run(args.output_dir.resolve(), modes)
    else:
        with tempfile.TemporaryDirectory(prefix='xita-async-reports-') as tmp:
            run(Path(tmp), modes)
