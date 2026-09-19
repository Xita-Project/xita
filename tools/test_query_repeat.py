#!/usr/bin/env python3
"""Exercise the exact production query-input diagnostic, with no guest execution."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix='xita-query-repeat-') as directory:
        binary = Path(directory) / 'query-repeat'
        command = ['cc', '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(ROOT / 'recomp/kernel')]
        if os.environ.get('SANITIZE'):
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command + [str(ROOT / 'tools/tests/query_repeat.c'),
            str(ROOT / 'recomp/kernel/xk_query_repeat.c'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == '__main__':
    main()
