#!/usr/bin/env python3
"""Production private-math ownership and concurrency; not whole-engine safety."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
hierarchy_build=os.environ.get('OBJECT_HIERARCHY_TEST_BUILD','0')=='1'
hierarchy_shared=os.environ.get('OBJECT_HIERARCHY_SHARED_TEST','0')=='1'
assert not hierarchy_shared or hierarchy_build
point_build=os.environ.get('OBJECT_POINT_TEST_BUILD','1')!='0'
quat_build=os.environ.get('OBJECT_QUAT_TEST_BUILD','1')!='0'
quat_cache=os.environ.get('OBJECT_QUAT_CACHE_TEST_BUILD','0')=='1'
constants_original=os.environ.get('OBJECT_QUAT_CONSTANT_MODE','original')=='original'
quat_profile=os.environ.get('OBJECT_QUAT_PROFILE_TEST_BUILD','0')=='1'
query_unlock=os.environ.get('OBJECT_QUERY_UNLOCK_TEST_BUILD','0')=='1'
owner_lane=os.environ.get('OBJECT_OWNER_LANE_TEST_BUILD','0')=='1'
three_workers=os.environ.get('OBJECT_THREE_WORKERS_TEST_BUILD','0')=='1'
NL=3 if three_workers else 2   # compiled worker lanes
with tempfile.TemporaryDirectory(prefix='xita-object-private-math-') as directory:
    binary=Path(directory)/'test'
    subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-std=gnu11',
        '-fno-strict-aliasing','-ffp-contract=off','-ffunction-sections','-fdata-sections',
        '-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_NATIVE_OBJECT_BASIS',
        *(['-DXV_HIERARCHY_SNAPSHOT=1','-DXV_NATIVE_MODEL_HIERARCHY','-DXV_HIERARCHY_FINAL_NORMAL=1','-DXV_HIERARCHY_MATRIX_NORMAL=1','-DTEST_HIERARCHY_INTEGRATION'] if hierarchy_build else []),
        *(['-DXV_HIERARCHY_ASSIST=1','-DTEST_HIERARCHY_SHARED'] if hierarchy_shared else []),
        *(['-DXV_OBJECT_POINT_EXPERIMENT'] if point_build else []),
        *(['-DXV_OBJECT_QUAT_PROFILE'] if quat_profile else []),
        *(['-DXV_QUERY_UNLOCK=1','-DXV_QUERY_UNLOCK_DEFAULT=1'] if query_unlock else []),
        *(['-DXV_OBJECT_OWNER_LANE=1','-DXV_OBJECT_OWNER_LANE_DEFAULT=1','-DXV_OBJECT_JOB_SPLIT=1','-DXV_OBJECT_JOB_SPLIT_DEFAULT=1'] if owner_lane else []),
        *(['-DXV_OBJECT_WORKERS=3','-DXV_OBJECT_JOB_SPLIT=1','-DXV_OBJECT_JOB_SPLIT_DEFAULT=1'] if three_workers else []),
        *(['-DXV_OBJECT_QUAT_EXPERIMENT'] if quat_build else []),
        *(['-DXV_QUAT_CACHE'] if quat_cache else []),
        '-I'+str(root/'recomp'),*shlex.split(os.environ.get('OBJECT_JOB_TEST_FLAGS','')),
        str(root/'tools/tests/object_private_math.c'),str(root/'recomp/kernel/xk_object_jobs.c'),
        str(root/'recomp/kernel/xk_math.c'),str(root/'recomp/kernel/xk_object_basis.c'),
        *([str(root/'recomp/kernel/xk_quat_cache.c')] if quat_cache else []),
        *([str(root/'recomp/kernel/xk_hierarchy.c'),'-Wl,--wrap=xv_object_hierarchy_suspend,--wrap=xv_object_hierarchy_resume'] if hierarchy_build else []),
        str(root/'recomp/xv_x86rt.c'),'-pthread','-lm','-Wl,--gc-sections','-o',str(binary)],check=True)
    for quat in ('0','1'):
        for point in ("0", "1"):
            for timed in ("0", "1"):
                for workers,private,fast in ((('3','1','1'),('3','0','1'),('3','1','0')) if three_workers else ())+(('2','1','1'),('1','1','1'),('0','1','1'),('2','0','1'),('2','1','0')):
                    env=dict(os.environ,XV_OBJECT_JOB_WORKERS=workers,XV_OBJECT_PRIVATE_MATH=private,XV_OBJECT_OWNER_LANE='1' if owner_lane else '0',
                             XV_OBJECT_LOCK_FAST_PATH=fast,XV_OBJECT_TIMED_WAIT=timed,XV_OBJECT_PRIVATE_POINT=point, XV_OBJECT_PRIVATE_QUATERNION=quat)
                    # With DEFAULT=1, exercise the real unset-environment startup
                    # path as well as the explicit runtime disable.
                    if os.environ.get('OBJECT_QUAT_TEST_ENV_UNSET')=='1' and quat=='1':
                        env.pop('XV_OBJECT_PRIVATE_QUATERNION')
                    result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30,env=env)
                    assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
                    if owner_lane:
                        ol=re.findall(r'\[owner-lane\] \d+ frames enabled (\d+) batches (\d+) jobs (\d+) pause-breaks (\d+) lock-spins (\d+) quiesce (\d+)',result.stderr)
                        assert len(ol)==2,ol
                        jobs=int(ol[0][2]);batches=int(ol[0][1])
                        assert (jobs>0)==(workers=='2'),(ol,workers)
                        print(f"owner lane: batches={batches} jobs={jobs} breaks={ol[0][3]} spins={ol[0][4]} quiesce={ol[0][5]}",flush=True)
                    if query_unlock:
                        qu=re.findall(r'\[query-unlock\] \d+ frames enabled (\d+) idle/caller/nested/state/disabled/profile/pause/stack/ready ([0-9/]+); unlocked (\d+)/(\d+)(?:/(\d+))? calls',result.stderr)
                        assert len(qu)==2,qu
                        reasons=list(map(int,qu[0][1].split('/')))
                        ready=reasons[8];calls=int(qu[0][2])+int(qu[0][3])+int(qu[0][4] or 0)
                        assert calls==ready,(qu,calls,ready)
                        assert (ready>0)==(workers!='0' and fast=='1'),(qu,workers,fast)
                        assert all(row[1]=='0/0/0/0/0/0/0/0/0' for row in qu[1:]),qu
                        print(f"query unlock: ready={ready} reasons={reasons}",flush=True)
                    if hierarchy_build:
                        hs=re.findall(r'\[hierarchy-ownership\] \d+ frames .*? ([0-9/]+)\n',result.stderr)
                        assert len(hs)==4,hs
                        counts=list(map(int,hs[0].split('/')))
                        assert len(counts)==9 and all(row=='0/0/0/0/0/0/0/0/0' for row in hs[1:]),hs
                        assert counts[8]==((600 if hierarchy_shared else 1200) if workers!='0' and private==fast=='1' else 0),hs
                        if hierarchy_shared:
                            assists=re.findall(r'\[hierarchy-assist\] \d+ frames offers (\d+) helped (\d+) local (\d+)',result.stderr)
                            assert len(assists)==2 and assists[1]==('0','0','0'),assists
                            offered,helped,local=map(int,assists[0])
                            assert offered==(600 if workers=='2' and fast=='1' else 0),assists
                            assert helped+local==offered,assists
                            print(f"hierarchy assistance: offers={offered} helped={helped} local={local}",flush=True)
                    quat_rows=re.findall(r'\[object-quat-site\] lane (\d+) pc ([0-9A-F]+) count (\d+) private-input (\d+)',result.stderr)
                    if quat_profile and not quat_cache and workers!='0' and private==fast=='1':
                        bypass=quat_build and quat=='1' and constants_original
                        assert sum(int(r[2]) for r in quat_rows)==(300 if bypass else 6600),quat_rows
                        assert sum(int(r[3]) for r in quat_rows)==(0 if bypass else 6300),quat_rows
                        assert sum(int(r[2]) for r in quat_rows if int(r[1],16)==0xA0010)==300,quat_rows
                    else:assert not quat_rows,quat_rows
                    rows=re.findall(r'\[object-private-math\] lane (\d) (\w+) attempted (\d+) released (\d+) nested (\d+) shared (\d+) disabled (\d+)',result.stderr)
                    for kind in ('point','matrix','quaternion','basis'):
                        releases=sum(int(row[3]) for row in rows if row[1]==kind)
                        if workers!='0' and private==fast=='1' and not (quat_cache and kind=='quaternion'):
                            assert releases>0,(kind,rows)
                        else: assert releases==0,(kind,rows)
                    if os.environ.get('OBJECT_HOLD_TEST'):
                        holds=re.findall(r'\[object-holds\] (\d+) frames lane (\d+) enabled (\d+) outer (\d+) samples (\d+) denominator 64;',result.stderr)
                        assert len(holds)==2*NL and all(row[2]=='1' for row in holds),holds
                        assert all(row[3:]==('0','0') for row in holds[NL:]),holds
                        acquisitions=re.search(r'acquired ((?:\d+/)+\d+) nested',result.stderr)[1].split('/')
                        for lane in range(NL):
                            assert int(holds[lane][3])==(int(acquisitions[lane]) if fast=='1' else 0)
                        assert (sum(int(row[4]) for row in holds)>0)==(workers!='0' and fast=='1')
                    native_rows=re.findall(r'\[native-point\] \d+ frames fast (\d+); fallback disabled (\d+) fp (\d+) layout (\d+) numeric (\d+)',result.stderr)
                    assert len(native_rows)==2 and list(map(int,native_rows[-1]))==[0]*5
                    point_rows=re.findall(r'\[object-point\] lane (\d) checks (\d+) private (\d+) nested (\d+) shared-input (\d+) shared-output (\d+)',result.stderr)
                    assert len(point_rows)==2*NL
                    assert all(list(map(int,row[1:]))==[0]*5 for row in point_rows[-NL:])
                    total=sum(int(row[2]) for row in point_rows)
                    assert (total>0)==(point_build and point=='1' and workers!='0' and fast=='1')
                    sites=re.findall(r'\[object-point-site\] lane (\d) pc ([0-9A-F]+) count (\d+) varied (\d+)[^\n]+overflow (\d)',result.stderr)
                    for lane in range(NL):
                        assert sum(int(r[2]) for r in sites if int(r[0])==lane)==sum(int(r[1]) for r in point_rows if int(r[0])==lane)
                    assert all(int(r[3])<int(r[2]) for r in sites)
                    for row in point_rows:
                        n=list(map(int,row));assert n[1]==sum(n[2:])
                    quat_stats=re.findall(r'\[object-quat\] lane (\d) checks (\d+) private (\d+) nested (\d+) shared-input (\d+) shared-output (\d+) constants (\d+)',result.stderr)
                    if quat_build:
                        assert len(quat_stats)==2*NL
                        assert all(list(map(int,r[1:]))==[0]*6 for r in quat_stats[-NL:])
                        assert (sum(int(r[2]) for r in quat_stats)>0)==(quat=='1' and not quat_cache and constants_original and workers!='0' and fast=='1')
                        assert all(int(r[1])==sum(map(int,r[2:])) for r in quat_stats)
                    else:assert not quat_stats
                    print(f'quat={quat}, point={point}, timed={timed}, workers={workers}, private={private}, fast={fast}: {result.stdout.strip()}')
