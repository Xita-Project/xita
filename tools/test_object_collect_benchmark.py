#!/usr/bin/env python3
"""Check optional object collection comparison admission and exact restoration."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xita-object-collect-') as directory:
    for absent in (False,True):
        binary=Path(directory)/('absent' if absent else 'controller')
        subprocess.run(['cc','-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror',
            '-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined',
            '-fno-omit-frame-pointer','-no-pie',
            *(['-DTEST_NO_OBJECT_COLLECT'] if absent else []),
            str(ROOT/'tools/tests/object_collect_benchmark.c'),'-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
