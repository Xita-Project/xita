#!/usr/bin/env python3
"""Test production job ownership using parallel synthetic object callbacks.

This validates the worker pool, not Halo's unproven shared object dependencies.
"""
from pathlib import Path
import os
import resource
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xita-object-jobs-') as directory:
    binary=Path(directory)/'test'
    subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-std=gnu11','-fno-strict-aliasing',
        '-DXV_EXPERIMENTAL_OBJECT_JOBS','-I'+str(root/'recomp'),
        '-ffunction-sections','-fdata-sections','-ffp-contract=off',
        *shlex.split(os.environ.get('OBJECT_JOB_TEST_FLAGS','')),
        str(root/'tools/tests/object_jobs.c'),str(root/'recomp/kernel/xk_object_jobs.c'),
        str(root/'recomp/kernel/xk_math.c'),str(root/'recomp/xv_x86rt.c'),
        '-pthread','-Wl,--gc-sections','-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=30)
    subprocess.run([str(binary),"default-on"],check=True,timeout=10)
    def no_core(): resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    failure=subprocess.run([str(binary),'unsupported-hle'],capture_output=True,
                           timeout=10,preexec_fn=no_core)
    assert failure.returncode<0 and b'STOP unsupported HLE target 001D66EC' in failure.stderr
    print('PASS: unsupported file-write HLE stops before invocation')
