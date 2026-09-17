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
    volume=re.search(r'^void xv_hle_CDirectSoundStream_SetVolume\(xctx \*c\).*$',d3d_source,re.M)[0]
    commit_macro=re.search(r'^#define DS_OK\(name, n\).*$',d3d_source,re.M)[0]
    commit=re.search(r'DS_OK\(IDirectSound_CommitDeferredSettings, 1\)',d3d_source)[0]
    buffers=d3d_source[d3d_source.index('#define DS_MAX_BUFFERS'):d3d_source.index('static uint32_t ds_buffer_obj')]
    voice_state=d3d_source[d3d_source.index('static ds_state *ds_voice_state'):d3d_source.index('void xv_hle_DSoundVoiceIsPlaying')]
    voice_stop=d3d_source[d3d_source.index('void xv_hle_DSoundVoiceStop'):d3d_source.index('void xv_hle_DirectSoundCreateBuffer')]
    frequency=d3d_source[d3d_source.index('void xv_hle_CDirectSoundStream_SetFrequency'):d3d_source.index('void xv_hle_CDirectSoundStream_SetVolume')]
    spatial='\n'.join(re.search(r'DS_OK\('+name+r', \d+\)',d3d_source)[0] for name in
        ('IDirectSoundStream_SetMaxDistance','IDirectSoundStream_SetMinDistance',
         'IDirectSoundStream_SetConeAngles','IDirectSoundStream_SetConeOutsideVolume','IDirectSoundStream_SetI3DL2Source'))
    fixture=(root/'tools/tests/object_audio.c').read_text()
    audio.write_text(fixture.replace('/* PRODUCTION_AUDIO */',d3d_source[begin:end]+'\n'+methods+'\n'+pump+'\n'+volume+'\n'+commit_macro+'\n'+commit+'\n'+spatial+'\n#undef DS_OK\n'+buffers+voice_state+voice_stop+frequency))
    subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-std=gnu11','-fno-strict-aliasing',
        '-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_NATIVE_MODEL_HIERARCHY','-I'+str(root/'recomp'),
        '-ffunction-sections','-fdata-sections','-ffp-contract=off',
        *shlex.split(os.environ.get('OBJECT_JOB_TEST_FLAGS','')),
        str(root/'tools/tests/object_jobs.c'),str(d3d),str(audio),str(root/'recomp/kernel/xk_object_jobs.c'),
        str(root/'recomp/kernel/xk_math.c'),str(root/'recomp/xv_x86rt.c'),
        str(root/'recomp/kernel/xk_hierarchy.c'),
        *([os.environ['OBJECT_SOLVER_BODY']] if os.environ.get('OBJECT_SOLVER_BODY') else []),
        '-pthread','-Wl,--gc-sections','-lm','-o',str(binary)],check=True)
    for timed in ("0", "1"):
        for profile in ("0", "1"):
            for workers in ("2", "1", "0"):
                env=dict(os.environ,XV_OBJECT_JOB_WORKERS=workers,XV_OBJECT_LOCK_PROFILE=profile,XV_OBJECT_TIMED_WAIT=timed)
                result=subprocess.run([str(binary)],check=True,timeout=30,env=env,
                                      capture_output=True,text=True)
                print(result.stdout,end='')
                assert re.findall(r'\[model-hierarchy\] 3 frames batches (\d+) child nodes (\d+)',result.stderr)==[('600','3600'),('0','0')]
                assert re.findall(r'quiescent owner stream volume updates (\d+)',result.stderr)==['600','0']
                assert re.findall(r'quiescent owner deferred audio commits (\d+)',result.stderr)==['600','0']
                assert re.findall(r'quiescent owner stream-start commits (\d+)',result.stderr)==['300','0']
                assert re.findall(r'quiescent owner stream status (\d+) packets (\d+)',result.stderr)==[('600','600'),('0','0')]
                assert re.findall(r'quiescent owner voice stops (\d+)',result.stderr)==['600','0']
                assert re.findall(r'quiescent owner frequency updates (\d+) spatial parameters (\d+)',result.stderr)==[('600','3000'),('0','0')]
                timed_rows=re.findall(r'\[object-wait\] timed (\d) attempts (\d+)/(\d+) acquired (\d+)/(\d+) timeouts (\d+)/(\d+)',result.stderr)
                assert len(timed_rows)==2 and timed_rows[1]==(timed,'0','0','0','0','0','0'),timed_rows
                values=list(map(int,timed_rows[0]));assert values[0]==int(timed)
                for lane in range(2):assert values[1+lane]==values[3+lane]+values[5+lane]
                if timed=='0' or workers!='2':assert not any(values[1:])
                else:assert sum(values[5:])>0,'held owner-service transactions must exercise timeout/park'
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
                if os.environ.get('OBJECT_HOLD_TEST'):
                    holds=re.findall(r'\[object-holds\] (\d+) frames lane (\d+) enabled (\d+) outer (\d+) samples (\d+) denominator 64;',result.stderr)
                    assert len(holds)==4 and all(row[0]=='3' and row[2]=='1' for row in holds),holds
                    assert all(row[3:] == ('0','0') for row in holds[2:]),holds
                    acquisitions=re.search(r'acquired (\d+)/(\d+)/(\d+) nested',result.stderr)
                    sites=re.findall(r'\[object-hold-site\] lane (\d+) pc ([0-9A-F]+) samples (\d+) elapsed-us (\d+) max-us (\d+) overflow (\d+)',result.stderr)
                    for lane in range(2):
                        rows=[tuple(int(v,16 if j==1 else 10) for j,v in enumerate(row)) for row in sites if int(row[0])==lane]
                        assert int(holds[lane][3])==int(acquisitions[lane+1]),(holds,acquisitions.groups())
                        assert sum(row[2] for row in rows)==int(holds[lane][4])
                        assert len(rows)<=33 and all(row[4]<=row[3] and (row[1] or row[5]) for row in rows)
                    if workers!='0':assert sum(int(row[4]) for row in holds)>0
                    else:assert not sites
                    children=re.findall(r'\[object-hold-child\] lane (\d+) parent 0004C980 child ([0-9A-F]+) samples (\d+) elapsed-us (\d+) max-us (\d+);',result.stderr)
                    for lane in range(2):
                        rows=[tuple(int(v,16 if j==1 else 10) for j,v in enumerate(row)) for row in children if int(row[0])==lane]
                        assert all(row[1] in (0x48090,0x4B9D0) and row[4]<=row[3] for row in rows)
                        if rows:assert len(rows)==2 and rows[0][2]==rows[1][2]
                        held_us=sum(int(row[3]) for row in sites if int(row[0])==lane)
                        assert sum(row[3] for row in rows)<=held_us
                    if workers!='0':assert children,'sampled child/owner-service attribution missing'
                    else:assert not children
                print(f'PASS: {workers} workers, profile {profile}, bounded wait {timed}: two object passes in three render frames; retired/reset totals and wait-site accounting')
    subprocess.run([str(binary),"default-on"],check=True,timeout=10)
    if '-DXV_OBJECT_SOLVER_EXPERIMENT' in os.environ.get('OBJECT_JOB_TEST_FLAGS',''):
        for reason in ('OBJECT_SOLVER_TEST_OFF','OBJECT_SOLVER_TEST_BAD_CONSTANT'):
            result=subprocess.run([str(binary)],check=True,timeout=30,capture_output=True,text=True,
                env=dict(os.environ,**{reason:'1'},XV_OBJECT_JOB_WORKERS='2'))
            assert 'solver boundary entered 0 scopes' in result.stdout
            print('PASS: solver retains transaction:',reason)
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
                        ("unsupported-stop",b'voice stop outside audited object sound cleanup'),
                        ("unsupported-stop-null",b'unsupported HLE'),
                        ("unsupported-commit",b'deferred audio commit outside audited sound update'),
                        ("unsupported-volume",b'stream volume outside audited object sound update'),
                        ("unsupported-stream",b'stream service outside audited refill'),
                        ("unsupported-stream-process",b'stream service outside audited refill'),
                        ("unsupported-nested-audio",b'unsupported nested owner audio service')):
        failure=subprocess.run([str(binary),mode],capture_output=True,timeout=10,preexec_fn=no_core)
        assert failure.returncode<0 and reason in failure.stderr,(mode,failure.stderr)
    print('PASS: unrelated audio/refill callers and recursive owner RPCs stop before invocation')
    for address in ('194470','193D9B','193DB3','193D68','193D96','193E22'):
        failure=subprocess.run([str(binary),'unsupported-parameter',address],capture_output=True,timeout=10,preexec_fn=no_core)
        assert failure.returncode<0 and b'stream parameter outside audited object sound update' in failure.stderr
    print('PASS: frequency and five spatial-property calls reject unrelated callers')
