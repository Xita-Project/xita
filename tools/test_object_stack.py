#!/usr/bin/env python3
"""Exercise original guest stack allocation on the production object workers."""
import argparse
import os
from pathlib import Path
import re
import resource
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--recomp', type=Path, required=True)
args = parser.parse_args()
bodies = []
for path in args.recomp.glob('code_*.c'):
    match = re.search(r'void f_0001D130\(xctx \*restrict c\)\n\{.*?\n\}', path.read_text(), re.S)
    if match:
        bodies.append(match[0])
assert len(bodies) == 1 and 'xv_object_job_stack_probe(c)' in bodies[0]
with tempfile.TemporaryDirectory(prefix='xita-object-stack-') as directory:
    body = Path(directory) / 'original.c'
    binary = Path(directory) / 'test'
    body.write_text('#include "kernel/xk_object_jobs.h"\n' + bodies[0])
    subprocess.run([os.environ.get('CC', 'cc'), '-O2', '-g', '-std=gnu11',
                    '-fno-strict-aliasing', '-DXV_EXPERIMENTAL_OBJECT_JOBS',
                    '-I' + str(root / 'recomp'), '-ffunction-sections', '-fdata-sections',
                    *shlex.split(os.environ.get('OBJECT_JOB_TEST_FLAGS', '')),
                    str(body), str(root / 'tools/tests/object_stack.c'),
                    str(root / 'recomp/kernel/xk_object_jobs.c'),
                    '-pthread', '-lm', '-Wl,--gc-sections', '-o', str(binary)], check=True)
    for workers in ('2', '1', '0'):
        env = dict(os.environ, XV_OBJECT_JOB_WORKERS=workers)
        subprocess.run([str(binary)], env=env, check=True, timeout=20)
        def no_core():
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        failed = subprocess.run([str(binary), 'overflow'], env=env,
                                capture_output=True, timeout=20, preexec_fn=no_core)
        assert failed.returncode < 0
        assert b'STOP guest stack allocation exceeds worker capacity' in failed.stderr
        assert b'canary 584A4F42' in failed.stderr
    print('PASS: over-capacity allocations stop before the original write; canary intact on every lane configuration')
