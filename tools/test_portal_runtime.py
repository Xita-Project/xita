#!/usr/bin/env python3
"""Compare actual retained/new ARM portal units and the guarded production adapter.

No scheduling or owner admission is faked into the candidate code: the platform
fixture supplies the same explicit boundary service to both linked runtimes.
Real object-backend admission tests are a separate required qualification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import test_native_visibility_portal_loop as vp
from test_arm_model_palette import RAM,CTX,SIZE
from unicorn.arm_const import UC_ARM_REG_FPSCR

def build(reference,candidate,out):
    flags=['-O2','-std=gnu11','-mthumb','-mcpu=cortex-a9','-mfpu=neon',
           '-I'+str(candidate/'recomp'),'-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_LIGHT_QUERY_CENSUS']
    cc=str(Path.home()/'vitasdk/bin/arm-vita-eabi-gcc');commands=[];objects=[]
    for name in ('visibility_portal_arm','portal_runtime_arm'):
        obj=out/(name+'.o');cmd=[cc,*flags,'-c',str(ROOT/'tools/tests'/(name+'.c')),'-o',str(obj)]
        subprocess.run(cmd,check=True);commands.append(cmd);objects.append(str(obj))
    names=['code_000.o','code_002.o','code_009.o','code_010.o','code_016.o','xv_x86rt.o','kernel/xk_clip.o',
           'kernel/xk_clip_region.o','kernel/xk_clip_region_control.o','kernel/xk_math.o','kernel/xk_light_census.o']
    hashes={}
    for lane,stage in (('reference',reference),('candidate',candidate)):
        selected=names+(['kernel/xk_portal_polygon.o','kernel/xk_portal_polygon_math.o'] if lane=='candidate' else [])
        retained=[stage/'build/recomp'/n for n in selected]
        hashes[lane]={n:hashlib.sha256(p.read_bytes()).hexdigest() for n,p in zip(selected,retained)}
        cmd=[cc,*flags,*objects,*map(str,retained),'-nostdlib',
             '-Wl,-Ttext=0x10000,-e,test_boot,--unresolved-symbols=ignore-all,--wrap=xv_preempt',
             '-lm','-lgcc','-o',str(out/(lane+'.elf'))]
        subprocess.run(cmd,check=True);commands.append(cmd)
    (out/'build.json').write_text(json.dumps(dict(commands=commands,retained_sha256=hashes),indent=2)+'\n')

original_fixture=vp.fixture
def fixture(layout,spec):
    memory,context,pages=original_fixture(layout,spec)
    if spec.get('outer'):
        memory=bytearray(memory)
        for i,v in enumerate((-1.,1.,-1.,1.)):
            a=0x2febe4+i*4;data=struct.pack('<f',v)
            for j,b in enumerate(data):memory[pages[(a+j)>>12]+((a+j)&4095)]=b
        memory=bytes(memory)
    return memory,context,pages
vp.fixture=fixture

class Machine(vp.Machine):
    def call(self,name,args=(),fpscr=0,active=False):
        if active and self.case.get('outer') and name=='f_000532E0':name='f_00053540'
        return super().call(name,args,fpscr,active)
    def result(self,spec):
        self.call('portal_fixture_boot')
        if spec.get('decline'):self.put(self.symbols['portal_test_decline'],spec['decline'])
        row=self.run(spec,False);u=self.uc
        external=bytes(u.mem_read(RAM,0x740000))+bytes(u.mem_read(RAM+0x780000,SIZE-0x780000))
        context=bytes(u.mem_read(CTX,self.layout['size']))
        preserved={f'r{i}':struct.unpack_from('<I',context,4*i)[0] for i in (3,4,5,6,7)}
        for n in ('fsp','preempt'):preserved[n]=struct.unpack_from('<I',context,self.layout[n])[0]
        return dict(external=external,preserved=preserved,context=context.hex(),
            fpscr=u.reg_read(UC_ARM_REG_FPSCR),stats=row['stats'],
            accepted=self.word(self.symbols['portal_accepted']) if 'portal_accepted' in self.symbols else 0,
            declines=list(struct.unpack('<7I',u.mem_read(self.symbols['portal_declines'],28))) if 'portal_declines' in self.symbols else [])

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference',type=Path,required=True);p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--reuse',action='store_true')
    p.add_argument('--case-prefix',action='append',default=[])
    a=p.parse_args();a.out=a.out.resolve()
    if a.out.is_relative_to(ROOT):p.error('private outputs must stay outside source')
    a.out.mkdir(parents=True,exist_ok=a.reuse)
    if not a.reuse:build(a.reference.resolve(),a.candidate.resolve(),a.out)
    specs=[dict(name='leaf',clusters=1,edges=[]),dict(name='projection',projection=True),
        dict(name='crossing',projection=True,portal_points=[(-2,-.5,1),(2,-.5,1),(2,.5,1),(-2,.5,1)]),
        dict(name='empty',projection=True,portal_points=[(2,2,1),(3,2,1),(3,3,1),(2,3,1)]),
        dict(name='diamond',projection=True,edges=[(0,1),(0,2),(1,3),(2,3)]),
        dict(name='noncontiguous',projection=True,noncontiguous=True),
        dict(name='low-budget',projection=True,budget=1,refill=1,fallback=True),
        dict(name='queued',projection=True,decline=6,fallback=True),
        dict(name='mode-off',projection=True,native_clip=0,fallback=True)]
    for mode in range(16):specs.append(dict(name=f'fp-{mode}',projection=True,fpscr=mode<<22))
    clockwise=[(-.5,-.5,1),(-.5,.5,1),(.5,.5,1),(.5,-.5,1)]
    for depth in (4,16,32):
        specs.append(dict(name=f'deep-{depth}',projection=True,clusters=depth,
            edges=[(i,i+1) for i in range(depth-1)],portal_points=clockwise,portal_vertex_base=0x430000,
            budget=100000,expect_depth=depth))
    for mode in (0,0x400000,0x800000,0xc00000,0x0300009f):
        specs.append(dict(name=f'outer-{mode:x}',projection=True,outer=True,clusters=4,
            edges=[(0,1),(1,2),(2,3)],portal_points=clockwise,budget=100000,fpscr=mode,expect_depth=4))
    if a.case_prefix:specs=[s for s in specs if any(s['name'].startswith(x) for x in a.case_prefix)]
    rows=[]
    for spec in specs:
        lanes={lane:Machine(a.out/(lane+'.elf')).result(spec) for lane in ('reference','candidate')}
        ref,cand=lanes.values();checks={k:ref[k]==cand[k] for k in ('external','preserved')}
        if spec.get('fallback'):checks['full_context']=ref['context']==cand['context'];checks['fpscr']=ref['fpscr']==cand['fpscr']
        for lane,r in lanes.items():
            if not all(checks.values()):(a.out/(spec['name']+'-'+lane+'.bin')).write_bytes(r['external'])
            r['external_sha256']=hashlib.sha256(r.pop('external')).hexdigest()
        rows.append(dict(spec=spec,checks=checks,lanes=lanes));(a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
        assert all(checks.values()),(spec['name'],checks)
        if spec.get('fallback') or spec['name']=='leaf':assert cand['accepted']==0
        else:assert cand['accepted']>0,(spec['name'],cand['declines'])
        print('PASS',spec['name'],{k:v['stats']['instructions'] for k,v in lanes.items()},'accepted',cand['accepted'],flush=True)

if __name__=='__main__':main()
