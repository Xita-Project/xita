#!/usr/bin/env python3
"""Exact pose-loop emission plus production worker guard and owner-service tests."""
import argparse
import os
from pathlib import Path
import re
import resource
import shlex
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from recompiler import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks


def region(body,address,name):
    prefix=body[:body.index(f'L_{address:08X}:')].replace(f'f_{address:08X}',name)
    start=body.index('L_0008E0F0:')
    end=body.index('    /* 0008E5D0 ',start)
    return prefix+body[start:end]+'    return;\n}\n'


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--xbe',required=True);ap.add_argument('--manifest',required=True)
    ap.add_argument('--output-dir',type=Path,required=True,help='Private generated evidence, outside Git')
    a=ap.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
    image=r.Image(a.xbe,a.manifest);hooks=HaloHooks(image);assert hooks.pose_coalesce_enabled
    baseline=HaloHooks(image);baseline.pose_coalesce_enabled=False
    read=image.bytes_at
    for address,size in ((0x8DDF0,2218),(0xB5B40,339),(0xB5F60,291)):
        def changed(p,n):
            data=bytearray(read(p,n))
            if (p,n)==(address,size):data[-1]^=1
            return bytes(data)
        image.bytes_at=changed
        altered=HaloHooks(image)
        assert not altered.pose_coalesce_enabled
        for entry in (0x8DDF0,0x8E087):assert 'POSE' not in '\n'.join(altered.function_entry(entry))
        for pc in (0x8E0F0,0x8E5D0):assert 'POSE' not in '\n'.join(altered.before_instruction(pc))
        image.bytes_at=read
    d=r.Discovery(image,{},image.kernel_imports(),lambda *args:None)
    for address in (0x8DDF0,0x8E087,0xB5B40,0xB5F60):
        d.add_root(address);d.lift_function(d.functions[address]);d.split_blocks(d.functions[address])
    old=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=baseline)
    new=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=hooks)
    cc=os.environ.get('CC','cc');include='-I'+str(ROOT/'recomp')
    generated=[]
    gate=re.compile(r'#if defined\(XV_EXPERIMENTAL_OBJECT_JOBS\) && defined\(XV_OBJECT_POSE_EXPERIMENT\)\n.*?\n#endif\n',re.S)
    for address,name in ((0x8DDF0,'pose_candidate'),(0x8E087,'pose_suffix')):
        before=old.emit_function(d.functions[address]);after=new.emit_function(d.functions[address])
        assert gate.sub('',after)==before,'Instruction body changed'
        assert after.count('XV_OBJECT_POSE_SCOPE()')==1
        assert after.count('XV_OBJECT_POSE_BEGIN(c)')==1
        assert after.count('XV_OBJECT_POSE_FINISH()')==1
        assert after.index('XV_OBJECT_POSE_BEGIN(c)')>after.index('L_0008E0F0:')
        assert after.index('XV_OBJECT_POSE_FINISH()')>after.index('L_0008E5D0:')
        with tempfile.TemporaryDirectory(prefix='xita-pose-disabled-') as temp:
            output=[]
            for label,body in (('before',before),('after',after)):
                p=Path(temp)/(label+'.c');p.write_text('#include "kernel/xk_object_jobs.h"\n'+body)
                output.append(subprocess.check_output([cc,'-E','-P',include,'-DXV_EXPERIMENTAL_OBJECT_JOBS',str(p)]))
            assert output[0]==output[1],'Compiled-out hooks change the translation'
        (a.output_dir/f'{address:08X}.c').write_text(after)
        generated.append(region(after,address,name))
        if address==0x8DDF0:generated.append(region(before,address,'pose_reference'))
    leaves=''.join(new.emit_function(d.functions[x]) for x in (0xB5B40,0xB5F60))
    source=a.output_dir/'pose-regions.c'
    source.write_text('#include "kernel/xk_object_jobs.h"\n'+leaves+''.join(generated))
    binary=a.output_dir/'pose-test'
    flags=shlex.split(os.environ.get('OBJECT_JOB_TEST_FLAGS',''))
    subprocess.run([cc,'-O2','-g','-std=gnu11','-fno-strict-aliasing','-ffp-contract=off',
        '-ffunction-sections','-fdata-sections','-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_OBJECT_POSE_EXPERIMENT',
        '-DXV_NATIVE_OBJECT_BASIS',include,*flags,str(source),str(ROOT/'tools/tests/object_pose.c'),
        str(ROOT/'recomp/kernel/xk_object_jobs.c'),str(ROOT/'recomp/kernel/xk_math.c'),
        str(ROOT/'recomp/kernel/xk_object_basis.c'),str(ROOT/'recomp/xv_x86rt.c'),
        '-pthread','-lm','-Wl,--gc-sections','-o',str(binary)],check=True)
    env=dict(os.environ,XV_NATIVE_MODEL_HIERARCHY='0',XV_OBJECT_LOCK_PROFILE='0',XV_OBJECT_TIMED_WAIT='0')
    for workers,fast in (('2','1'),('1','1'),('0','1'),('2','0')):
        trial=dict(env,XV_OBJECT_JOB_WORKERS=workers,XV_OBJECT_LOCK_FAST_PATH=fast)
        p=subprocess.run([str(binary)],env=trial,capture_output=True,text=True,timeout=40)
        (a.output_dir/f'workers-{workers}-fast-{fast}.log').write_text(p.stdout+p.stderr)
        assert p.returncode==0,(p.returncode,p.stdout,p.stderr)
        rows=re.findall(r'\[object-pose\] lane (\d) enabled (\d) entered (\d+) enclosing (\d+) finished (\d+) cleanup (\d+) recursive-locks (\d+)',p.stderr)
        assert len(rows)==16,rows
        acquired=re.findall(r'\[object-locks\].*? acquired (\d+)/(\d+)/(\d+)',p.stderr)
        assert len(acquired)==8
        totals=[sum(map(int,acquired[i])) for i in (0,4,6)]
        assert totals[0]==totals[2],('restoration changed acquisitions',totals)
        if workers!='0' and fast=='1':assert totals[1]<totals[0],totals
        else:assert totals[1]==totals[0],totals
        for pass_no in range(4):
            current=[list(map(int,x)) for x in rows[pass_no*4:pass_no*4+2]]
            reset=[list(map(int,x)) for x in rows[pass_no*4+2:pass_no*4+4]]
            assert all(not any(x[2:]) for x in reset),reset
            assert all(x[2]==x[4]+x[5] for x in current),current
            assert all(x[1]==(pass_no in (1,2)) for x in current),current
            active=workers!='0' and fast=='1' and pass_no in (1,2)
            assert (sum(x[2] for x in current)>0)==active,current
            if active:assert sum(x[3] for x in current)>0 and sum(x[5] for x in current)>0 and sum(x[6] for x in current)>0,current
        print(p.stdout,end='')
    def no_core():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    for mode,reason in (('live-override',None),('queued-override',None),('foreign-override',None),
                        ('unsupported-hle','STOP unsupported HLE'),('budget-stop','STOP job instruction budget exceeded')):
        p=subprocess.run([str(binary),mode],env=dict(env,XV_OBJECT_JOB_WORKERS='2',XV_OBJECT_LOCK_FAST_PATH='1'),
                         capture_output=True,text=True,timeout=10,preexec_fn=no_core)
        (a.output_dir/f'{mode}.log').write_text(p.stdout+p.stderr)
        assert p.returncode<0 and 'FORBIDDEN handler invoked' not in p.stderr,(mode,p.returncode,p.stderr)
        if reason:assert reason in p.stderr,(mode,p.stderr)
    print('PASS: live/queued/foreign override rejected; unsupported HLE and exhausted budget retain fatal STOP')
    print('PASS: supported signatures, both emitted copies, byte-identical disabled translation, no instruction changes')


if __name__=='__main__':main()
