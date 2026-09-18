#!/usr/bin/env python3
"""Host-only packet timing and production notification polling checks."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(out, modes):
    source = (ROOT / 'runtime/main.c').read_text()
    a = source.index('static struct {', source.index('static volatile int      g_running'))
    b = source.index('static xv_slot_owner', a)
    (out / 'packet_declarations.inc').write_text(source[a:b])
    a = source.index('static unsigned g_retired_count')
    b = source.index('/* Core 1 owns all GXM submission.', a)
    (out / 'packet_retire.inc').write_text(source[a:b])
    sdk = Path(os.environ.get('VITASDK', Path.home() / 'vitasdk'))
    for mode in modes:
        flags = []
        if mode == 'asan':
            flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        if mode == 'tsan':
            flags = ['-fsanitize=thread', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        exe = out / ('packet-' + mode)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g',
                        '-Wall', '-Wextra', '-Werror', '-DXV_GPU_PACKET_TIMING=1',
                        '-I' + str(ROOT), '-I' + str(ROOT / 'runtime'), '-I' + str(out), '-idirafter', str(sdk / 'arm-vita-eabi/include'),
                        str(ROOT / 'tools/tests/gpu_packet_timing.c'), '-pthread', *flags, '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=20,
                       env={**os.environ, 'ASAN_OPTIONS': 'abort_on_error=1:detect_leaks=1',
                            'TSAN_OPTIONS': 'halt_on_error=1'})
    # Production submission/retirement integration, compiled ON and OFF. This
    # fixture also checks display release, cap, failure drain and ticket wrap.
    for enabled in ('0', '1'):
        subprocess.run(['python3', str(ROOT / 'tools/test_frame_completion.py')], check=True,
                       env={**os.environ, 'TEST_GPU_PACKET_TIMING': enabled})


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
        with tempfile.TemporaryDirectory(prefix='xita-packet-timing-') as directory:
            run(Path(directory), modes)
