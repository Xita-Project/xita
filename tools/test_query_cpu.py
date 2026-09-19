#!/usr/bin/env python3
"""Test explicit CPU transactions, including optional EFLAGS.ID layouts."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix='xita-query-cpu-') as directory:
        command = shlex.split(os.environ.get('CC', 'cc'))
        command += ['-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fno-strict-aliasing',
                    '-I' + str(ROOT / 'recomp'), '-I' + str(ROOT / 'recomp/kernel')]
        if os.environ.get('SANITIZE'):
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        for id_flag in (0, 1):
            binary = Path(directory) / f'query-cpu-id{id_flag}'
            subprocess.run(command + [f'-DXV_EFLAGS_ID={id_flag}',
                str(ROOT / 'tools/tests/query_cpu.c'),
                str(ROOT / 'recomp/kernel/xk_query_cpu.c'),
                '-lm', '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == '__main__':
    main()
