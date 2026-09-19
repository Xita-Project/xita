#!/usr/bin/env python3
"""Test explicit capture notifications without generated-query integration."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix='xita-query-capture-') as directory:
        binary = Path(directory) / 'query-capture'
        command = shlex.split(os.environ.get('CC', 'cc'))
        command += ['-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(ROOT / 'recomp/kernel')]
        if os.environ.get('SANITIZE'):
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command + [str(ROOT / 'tools/tests/query_capture.c'),
            str(ROOT / 'recomp/kernel/xk_query_capture.c'),
            str(ROOT / 'recomp/kernel/xk_query_memory.c'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == '__main__':
    main()
