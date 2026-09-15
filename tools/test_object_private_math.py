#!/usr/bin/env python3
"""Production private-math ownership and concurrency; not whole-engine safety."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
point_build=os.environ.get('OBJECT_POINT_TEST_BUILD','1')!='0'
with tempfile.TemporaryDirectory(prefix='xita-object-private-math-') as directory:
    binary=Path(directory)/'test'
    subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-std=gnu11',
        '-fno-strict-aliasing','-ffp-contract=off','-ffunction-sections','-fdata-sections',
        '-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_NATIVE_OBJECT_BASIS',
        *(['-DXV_OBJECT_POINT_EXPERIMENT'] if point_build else []),
        '-I'+str(root/'recomp'),*shlex.split(os.environ.get('OBJECT_JOB_TEST_FLAGS','')),
        str(root/'tools/tests/object_private_math.c'),str(root/'recomp/kernel/xk_object_jobs.c'),
        str(root/'recomp/kernel/xk_math.c'),str(root/'recomp/kernel/xk_object_basis.c'),
        str(root/'recomp/xv_x86rt.c'),'-pthread','-lm','-Wl,--gc-sections','-o',str(binary)],check=True)
    for point in ("0", "1"):
        for timed in ("0", "1"):
            for workers,private,fast in (('2','1','1'),('1','1','1'),('0','1','1'),('2','0','1'),('2','1','0')):
                result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30,
                    env=dict(os.environ,XV_OBJECT_JOB_WORKERS=workers,XV_OBJECT_PRIVATE_MATH=private,
                             XV_OBJECT_LOCK_FAST_PATH=fast,XV_OBJECT_TIMED_WAIT=timed,XV_OBJECT_PRIVATE_POINT=point))
                assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
                rows=re.findall(r'\[object-private-math\] lane (\d) (\w+) attempted (\d+) released (\d+) nested (\d+) shared (\d+) disabled (\d+)',result.stderr)
                for kind in ('point','matrix','quaternion','basis'):
                    releases=sum(int(row[3]) for row in rows if row[1]==kind)
                    if workers!='0' and private==fast=='1':
                        assert releases>0,(kind,rows)
                    else: assert releases==0,(kind,rows)
                native_rows=re.findall(r'\[native-point\] \d+ frames fast (\d+); fallback disabled (\d+) fp (\d+) layout (\d+) numeric (\d+)',result.stderr)
                assert len(native_rows)==2 and list(map(int,native_rows[-1]))==[0]*5
                point_rows=re.findall(r'\[object-point\] lane (\d) checks (\d+) private (\d+) nested (\d+) shared-input (\d+) shared-output (\d+)',result.stderr)
                assert len(point_rows)==4
                assert all(list(map(int,row[1:]))==[0]*5 for row in point_rows[-2:])
                total=sum(int(row[2]) for row in point_rows)
                assert (total>0)==(point_build and point=='1' and workers!='0' and fast=='1')
                sites=re.findall(r'\[object-point-site\] lane (\d) pc ([0-9A-F]+) count (\d+) varied (\d+)[^\n]+overflow (\d)',result.stderr)
                for lane in range(2):
                    assert sum(int(r[2]) for r in sites if int(r[0])==lane)==sum(int(r[1]) for r in point_rows if int(r[0])==lane)
                assert all(int(r[3])<int(r[2]) for r in sites)
                for row in point_rows:
                    n=list(map(int,row));assert n[1]==sum(n[2:])
                print(f'point={point}, timed={timed}, workers={workers}, private={private}, fast={fast}: {result.stdout.strip()}')
