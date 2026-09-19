#!/usr/bin/env python3
"""Exercise the actual reuse adapter with deterministic query/owner/FP shims."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix='xita-query-reuse-') as directory:
        for sanitizer in (False, True):
            binary = Path(directory) / ('sanitize' if sanitizer else 'normal')
            command = shlex.split(os.environ.get('CC', 'cc'))
            command += ['-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fno-strict-aliasing', str(ROOT / 'tools/tests/query_reuse.c'),
                        str(ROOT / 'recomp/kernel/xk_query_cpu.c'),
                        str(ROOT / 'recomp/kernel/xk_query_memory.c'), '-o', str(binary)]
            if sanitizer:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == '__main__':
    main()
