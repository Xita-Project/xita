#!/usr/bin/env python3
"""Verify the production Vita mutex adapter with deterministic platform shims."""
import os
from pathlib import Path
import resource
import shlex
import signal
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
sdk=Path(os.environ.get('VITASDK',str(Path.home()/'vitasdk')))
with tempfile.TemporaryDirectory(prefix='xita-object-mutex-') as directory:
    binary=Path(directory)/'test'
    subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-std=gnu11','-D__vita__',
        '-Wall','-Wextra','-Werror','-I'+str(root/'recomp'),
        '-idirafter',str(sdk/'arm-vita-eabi/include'),
        *shlex.split(os.environ.get('OBJECT_JOB_TEST_FLAGS','')),
        str(root/'tools/tests/object_mutex.c'),'-pthread','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=20)
    def no_core():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    for light in ('0','1'):
        for fault in ('1','2','3','4'):
            result=subprocess.run([str(binary),light,fault],capture_output=True,
                timeout=10,preexec_fn=no_core)
            assert result.returncode==-signal.SIGABRT,(light,fault,result.returncode)
    print('PASS: bounded timeout, reset, unlock wake and unexpected try/lock/unlock/timed errors for both backends')
