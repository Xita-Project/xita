#!/usr/bin/env python3
"""Test production job ownership using parallel synthetic object callbacks.

This validates the worker pool, not Halo's unproven shared object dependencies.
"""
from pathlib import Path
import os
import resource
import re
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xita-object-jobs-') as directory:
    binary=Path(directory)/'test'
    # Compile the actual pointer-return handler; only its diagnostic counter is
    # supplied by the fixture. This keeps its ABI/result test tied to production.
    d3d_source=(root/'recomp/kernel/xd3d.c').read_text()
    handler=re.search(r'^void xv_hle_D3DVertexBuffer_Lock\(xctx \*c\) \{.*?\}$',d3d_source,re.M)
    assert handler
    d3d=Path(directory)/'vertex_lock.c'
    d3d.write_text('#include "kernel/xk.h"\n'
                   'void object_test_d3d_count(const char *);\n'
                   '#define XD3D_COUNT(name) object_test_d3d_count(name)\n'+
                   '\n'.join(re.search(r'^#define '+name+r'\(.*$',d3d_source,re.M)[0]
                             for name in ('RES_DATA','GUEST_PTR'))+'\n'+handler[0]+'\n')
    audio=Path(directory)/'audio_pump.c'
    # Actual queue completion, GetStatus and Process bodies. Platform audio and
    # original callback execution are supplied by a deterministic host fixture.
    begin=d3d_source.index('#define DS_MAX_STREAMS')
    end=d3d_source.index('/* debug: detect a stream',begin)
    methods=d3d_source[d3d_source.index('static void xv_hle_CDirectSoundStream_GetStatus'):d3d_source.index('static void xv_hle_CDirectSoundStream_Discontinuity')]
    pump=re.search(r'^void xv_hle_DirectSoundDoWork\(xctx \*c\).*$',d3d_source,re.M)[0]
    fixture=(root/'tools/tests/object_audio.c').read_text()
    audio.write_text(fixture.replace('/* PRODUCTION_AUDIO */',d3d_source[begin:end]+'\n'+methods+'\n'+pump))
    subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-std=gnu11','-fno-strict-aliasing',
        '-DXV_EXPERIMENTAL_OBJECT_JOBS','-I'+str(root/'recomp'),
        '-ffunction-sections','-fdata-sections','-ffp-contract=off',
        *shlex.split(os.environ.get('OBJECT_JOB_TEST_FLAGS','')),
        str(root/'tools/tests/object_jobs.c'),str(d3d),str(audio),str(root/'recomp/kernel/xk_object_jobs.c'),
        str(root/'recomp/kernel/xk_math.c'),str(root/'recomp/xv_x86rt.c'),
        '-pthread','-Wl,--gc-sections','-lm','-o',str(binary)],check=True)
    for profile in ("0", "1"):
        for workers in ("2", "1", "0"):
            env=dict(os.environ,XV_OBJECT_JOB_WORKERS=workers,XV_OBJECT_LOCK_PROFILE=profile)
            result=subprocess.run([str(binary)],check=True,timeout=30,env=env,
                                  capture_output=True,text=True)
            print(result.stdout,end='')
            reports=re.findall(r'^\[object-jobs\] (\d+) frames passes (\d+) batches (\d+) jobs (\d+)',result.stderr,re.M)
            assert reports==[('3','2','6','600'),('3','0','0','0')],reports
            locks=re.findall(r'contended (\d+)/(\d+) wait-us (\d+)/(\d+);',result.stderr)
            assert len(locks)==2 and locks[1]==('0','0','0','0'),locks
            sites=re.findall(r'^\[object-lock-site\] lane (\d+) pc ([0-9A-F]+) count (\d+) wait-us (\d+) max-us (\d+) overflow (\d+)',result.stderr,re.M)
            if profile=='0': assert not sites
            else:
                for lane in range(2):
                    rows=[tuple(int(x,16 if i==1 else 10) for i,x in enumerate(row))
                          for row in sites if int(row[0])==lane]
                    assert sum(row[2] for row in rows)==int(locks[0][lane])
                    assert sum(row[3] for row in rows)==int(locks[0][lane+2])
                    assert len(rows)<=33
                    assert all(row[4]<=row[3] and (row[1]!=0 or row[5]) for row in rows)
                if workers=='2': assert sites,'two contending worker callbacks must be attributed'
            print(f'PASS: {workers} workers, profile {profile}: two object passes in three render frames; retired/reset totals and wait-site accounting')
    subprocess.run([str(binary),"default-on"],check=True,timeout=10)
    def no_core(): resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    failure=subprocess.run([str(binary),'unsupported-hle'],capture_output=True,
                           timeout=10,preexec_fn=no_core)
    assert failure.returncode<0 and b'STOP unsupported HLE target 001D66EC' in failure.stderr
    print('PASS: unsupported file-write HLE stops before invocation')
    failure=subprocess.run([str(binary),'unsupported-yield'],capture_output=True,
                           timeout=10,preexec_fn=no_core)
    assert failure.returncode<0 and b'STOP yield outside audited cache wait' in failure.stderr
    print('PASS: unrelated guest yields stop before scheduling any fiber')
    failure=subprocess.run([str(binary),'unsupported-vertex-lock'],capture_output=True,
                           timeout=10,preexec_fn=no_core)
    assert failure.returncode<0 and b'STOP vertex lock outside audited impact transaction' in failure.stderr
    print('PASS: vertex locks outside the audited impact transaction stop before invocation')
    for mode,reason in (("unsupported-audio",b'audio pump outside audited cache callback'),
                        ("unsupported-stream",b'stream service outside quiescent audio callback'),
                        ("unsupported-nested-audio",b'unsupported nested owner audio service')):
        failure=subprocess.run([str(binary),mode],capture_output=True,timeout=10,preexec_fn=no_core)
        assert failure.returncode<0 and reason in failure.stderr,(mode,failure.stderr)
    print('PASS: unrelated audio callers, worker stream calls and recursive owner RPCs stop before invocation')
