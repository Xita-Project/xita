#!/usr/bin/env python3
"""Production private-math ownership and concurrency; not whole-engine safety."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xita-object-private-math-') as directory:
    binary=Path(directory)/'test'
    subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-std=gnu11',
        '-fno-strict-aliasing','-ffp-contract=off','-ffunction-sections','-fdata-sections',
        '-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_NATIVE_OBJECT_BASIS',
        '-I'+str(root/'recomp'),*shlex.split(os.environ.get('OBJECT_JOB_TEST_FLAGS','')),
        str(root/'tools/tests/object_private_math.c'),str(root/'recomp/kernel/xk_object_jobs.c'),
        str(root/'recomp/kernel/xk_math.c'),str(root/'recomp/kernel/xk_object_basis.c'),
        str(root/'recomp/xv_x86rt.c'),'-pthread','-lm','-Wl,--gc-sections','-o',str(binary)],check=True)
    for timed in ("0", "1"):
        for workers,private,fast in (('2','1','1'),('1','1','1'),('0','1','1'),('2','0','1'),('2','1','0')):
            result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30,
                env=dict(os.environ,XV_OBJECT_JOB_WORKERS=workers,XV_OBJECT_PRIVATE_MATH=private,
                         XV_OBJECT_LOCK_FAST_PATH=fast,XV_OBJECT_TIMED_WAIT=timed))
            assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
            rows=re.findall(r'\[object-private-math\] lane (\d) (\w+) attempted (\d+) released (\d+) nested (\d+) shared (\d+) disabled (\d+)',result.stderr)
            for kind in ('point','matrix','quaternion','basis'):
                releases=sum(int(row[3]) for row in rows if row[1]==kind)
                if workers!='0' and private==fast=='1':
                    assert releases>0,(kind,rows)
                else: assert releases==0,(kind,rows)
            print(f'timed={timed}, workers={workers}, private={private}, fast={fast}: {result.stdout.strip()}')
