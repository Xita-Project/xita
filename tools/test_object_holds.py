#!/usr/bin/env python3
"""Check diagnostic restoration and real concurrent guard sample accounting."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xita-object-holds-') as directory:
    for absent in (False,True):
        binary=Path(directory)/('absent' if absent else 'controller')
        subprocess.run(['cc','-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror',
            '-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined',
            '-fno-omit-frame-pointer','-no-pie',
            *(['-DTEST_NO_OBJECT_HOLDS'] if absent else []),
            str(ROOT/'tools/tests/object_holds_benchmark.c'),'-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    subprocess.run([sys.executable,str(ROOT/'tools/test_object_jobs.py')],check=True,
        env=dict(os.environ,OBJECT_HOLD_TEST='1',
            OBJECT_JOB_TEST_FLAGS=os.environ.get('OBJECT_JOB_TEST_FLAGS','')+
                ' -DXV_OBJECT_HOLD_PROFILE'))
