#!/usr/bin/env python3
"""Validate selected cache-fiber scheduling using the production kernel."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xita-object-io-') as tmp:
    binary=Path(tmp)/'test'
    subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-std=gnu11',
        '-fno-strict-aliasing','-DXV_EXPERIMENTAL_OBJECT_JOBS',
        '-ffunction-sections','-fdata-sections','-I'+str(root/'recomp'),
        *shlex.split(os.environ.get('OBJECT_IO_TEST_FLAGS','')),
        str(root/'tools/tests/object_io_fiber.c'),str(root/'recomp/kernel/xk_thread.c'),
        str(root/'recomp/kernel/xk_os_host.c'),'-Wl,--gc-sections','-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=10)
