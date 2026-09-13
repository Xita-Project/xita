#!/usr/bin/env python3
"""Verify bulk constant capture against the original row-by-row semantics."""
import os
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xita-constant-window-') as directory:
    binary=Path(directory)/'test'
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'runtime'),
                    str(root/'tools/tests/constant_window.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
