#!/usr/bin/env python3
"""Compare retained ARM visibility outputs with the actual whole-pass adapter.

Worker arithmetic is sequential in this ARM platform fixture. The real queue,
FP restoration, and capture/admission require the separate pthread tests too.
"""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
import test_subcluster_runtime as sr
import test_subcluster_pass as sp
from test_arm_model_palette import RAM,PT
ROOT=Path(__file__).resolve().parents[1]
def build(reference,candidate,out):
    sr.build(reference,candidate,out)
    record=json.loads((out/'build.json').read_text());cmd=record['commands'][0]
    # Keep this private harness away from a reproducible Unicorn Thumb IT-state
    # failure at 0x2a9400. Relinking the same production objects by 64 bytes
    # avoids it; neither the objects nor their instructions are rewritten.
    def relocate(cmd):
        return [x.replace('-Ttext=0x10000','-Ttext=0x10040') for x in cmd]
    ref_link=relocate(record['commands'][-2]);extra=[reference/'build/recomp/kernel'/n for n in ('xk_subcluster.o','xk_subcluster_math.o')]
    ref_link[ref_link.index('-nostdlib'):ref_link.index('-nostdlib')]=list(map(str,extra))
    subprocess.run(ref_link,check=True)
    for path in extra:record['retained_sha256']['reference']['kernel/'+path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
    flags=cmd[:cmd.index('-c')]+['-DXV_NATIVE_VISIBILITY_JOBS=1']
    fixture=out/'visibility_pass_arm.o'
    compile_cmd=[*flags,'-c',str(ROOT/'tools/tests/visibility_pass_arm.c'),'-o',str(fixture)]
    subprocess.run(compile_cmd,check=True)
    link=relocate(record['commands'][-1]);new=candidate/'build/recomp/kernel/xk_visibility_pass.o'
    link[link.index('-nostdlib'):link.index('-nostdlib')]=[str(fixture),str(new)]
    subprocess.run(link,check=True)
    record['commands']+=[ref_link,compile_cmd,link];record['retained_sha256']['candidate']['kernel/xk_visibility_pass.o']=hashlib.sha256(new.read_bytes()).hexdigest()
    (out/'build.json').write_text(json.dumps(record,indent=2)+'\n')

class PassMachine(sr.PassMachine):
    def prepare(self,memory,context,spec):
        super().prepare(memory,context,spec)
        struct.pack_into('<I',memory,0x400134,len(spec['clusters']))
    def step(self,u,address,size,user):
        if getattr(self,'target',None) and not self.remapped and address==(self.symbols[self.target]&~1):
            self.remapped=True
            if self.spec.get('remap'):
                for page in [0x420,0x421,0x450,0x30b]:
                    dst=0x680000+(page&255)*4096
                    u.mem_write(RAM+dst,bytes(u.mem_read(RAM+page*4096,4096)))
                    u.mem_write(PT+4*page,struct.pack('<I',dst))
        super().step(u,address,size,user)
    def result(self,spec):
        self.callbacks=[];self.remapped=False;self.spec=spec
        self.target='test_visibility_pass' if 'test_visibility_pass' in self.symbols else 'f_00052E10'
        r=self.run_pass(spec,self.target);r['callbacks']=self.callbacks
        r['whole_accepted']=struct.unpack('<I',self.uc.mem_read(self.symbols['visibility_pass_accepted'],4))[0] if 'visibility_pass_accepted' in self.symbols else 0
        r['whole_declines']=list(struct.unpack('<5I',self.uc.mem_read(self.symbols['visibility_pass_declined'],20))) if 'visibility_pass_declined' in self.symbols else []
        return r
class OuterMachine(sr.OuterMachine):
    def result(self,spec):
        r=super().result(spec)
        r['whole_accepted']=self.word(self.symbols['visibility_pass_accepted']) if 'visibility_pass_accepted' in self.symbols else 0
        r['whole_declines']=list(struct.unpack('<5I',self.uc.mem_read(self.symbols['visibility_pass_declined'],20))) if 'visibility_pass_declined' in self.symbols else []
        return r
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('reference','candidate','out'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--reuse',action='store_true');p.add_argument('--prefix',action='append',default=[]);a=p.parse_args();a.out=a.out.resolve()
    if a.out.is_relative_to(ROOT):p.error('private output required')
    a.out.mkdir(parents=True,exist_ok=a.reuse)
    if not a.reuse:build(a.reference.resolve(),a.candidate.resolve(),a.out)
    inside=[-.5,.5]*3;cross=[-.5,1.5]*3;outside=[3.,4.]*3
    clusters=[[(inside,[0,1,31,32,32]),(outside,[2,3]),(cross,[31,33,100])],[(cross,[100,200,511]),(inside,[]),(inside,[0,511,512])],[]]
    specs=[dict(name='empty',visible=[],clusters=[],fallback=True),dict(name='no-subclusters',visible=[2],clusters=clusters,fallback=True),
        dict(name='ordered',visible=[0,1,2],clusters=clusters),dict(name='duplicates',visible=[1,0,1],clusters=clusters),
        dict(name='global',visible=[0,1],clusters=clusters,global_frustum=1),dict(name='preset',visible=[0,1],clusters=clusters,preset=[31,100,511],initial=3),
        dict(name='capacity',visible=[0,1],clusters=clusters,initial=16382),dict(name='full',visible=[0,1],clusters=clusters,initial=16384,fallback=True),
        dict(name='bulk',visible=[0],clusters=[[(inside,list(range(1024))),(inside,list(range(768,1792)))]]),
        dict(name='queued',visible=[0,1],clusters=clusters,decline=6,fallback=True),dict(name='budget',visible=[0,1],clusters=clusters,budget=1,fallback=True),
        dict(name='remap',visible=[0,1],clusters=clusters,remap=True)]
    for count in (23,24,181,512,513):
        groups=[[(inside,[i%4096,(i+31)%4096]) for i in range(start,min(count,start+32))] for start in range(0,count,32)]
        specs.append(dict(name=f'packet-{count}',visible=list(range(len(groups))),clusters=groups,fallback=count>512))
    for mode in range(16):specs.append(dict(name=f'fp-{mode}',visible=[0,1],clusters=clusters,mode=mode<<22))
    points=[(-.5,-.5,1),(-.5,.5,1),(.5,.5,1),(.5,-.5,1)]
    for mode in (0,0x400000,0x800000,0xc00000,0x0300009f):
        specs.append(dict(name=f'outer-{mode:x}',outer=True,projection=True,clusters=4,edges=[(0,1),(1,2),(2,3)],portal_points=points,budget=100000,fpscr=mode))
    specs.append(dict(name='outer-budget',outer=True,projection=True,clusters=4,edges=[(0,1),(1,2),(2,3)],portal_points=points,budget=1,refill=17,fallback=True))
    sr.pr.vp.fixture=sr.outer_fixture
    if a.prefix:specs=[s for s in specs if any(s['name'].startswith(p) for p in a.prefix)]
    rows=[]
    for s in specs:
        cls=OuterMachine if s.get('outer') else PassMachine
        lanes={n:cls(a.out/(n+'.elf')).result(s) for n in ('reference','candidate')}
        checks={k:lanes['reference'][k]==lanes['candidate'][k] for k in ('external','preserved','callbacks')}
        if s.get('fallback'):checks.update({k:lanes['reference'][k]==lanes['candidate'][k] for k in ('context','fpscr')})
        if s.get('outer'):
            for r in lanes.values():r['external_sha256']=hashlib.sha256(r.pop('external')).hexdigest()
        rows.append(dict(spec=s,checks=checks,lanes=lanes));(a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
        assert all(checks.values()),(s['name'],checks)
        assert bool(lanes['candidate']['whole_accepted'])!=bool(s.get('fallback')),(s['name'],lanes['candidate'])
        print('PASS',s['name'],[(n,r.get('instructions',r.get('stats',{}).get('instructions'))) for n,r in lanes.items()],flush=True)
if __name__=='__main__':main()
