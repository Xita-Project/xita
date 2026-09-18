#!/usr/bin/env python3
"""Compare retained cumulative ARM runtimes through subcluster/visibility roots.

Actual generated/native objects execute; the shared idle-owner platform fixture
does not replace production worker admission qualification, tested separately.
"""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path
import test_subcluster_pass as sp
import test_portal_runtime as pr
from test_arm_model_palette import RAM,PT,CTX,STACK,END
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
ROOT=Path(__file__).resolve().parents[1]

def build(reference,candidate,out):
    flags=['-O2','-std=gnu11','-fno-strict-aliasing','-mthumb','-mcpu=cortex-a9','-mfpu=neon',
           '-I'+str(candidate/'recomp'),'-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_LIGHT_QUERY_CENSUS']
    cc=str(Path.home()/'vitasdk/bin/arm-vita-eabi-gcc');commands=[];fixtures=[]
    for name in ('visibility_portal_arm','portal_runtime_arm'):
        obj=out/(name+'.o');cmd=[cc,*flags,'-c',str(ROOT/'tools/tests'/(name+'.c')),'-o',str(obj)]
        subprocess.run(cmd,check=True);commands.append(cmd);fixtures.append(obj)
    names=['code_000.o','code_002.o','code_009.o','code_010.o','code_016.o','xv_x86rt.o',
           'kernel/xk_bounds.o','kernel/xk_clip.o','kernel/xk_clip_region.o','kernel/xk_clip_region_control.o',
           'kernel/xk_math.o','kernel/xk_light_census.o','kernel/xk_portal_polygon.o','kernel/xk_portal_polygon_math.o']
    hashes={}
    for lane,stage in [('reference',reference),('candidate',candidate)]:
        selected=names+(['kernel/xk_subcluster.o','kernel/xk_subcluster_math.o'] if lane=='candidate' else [])
        objects=[stage/'build/recomp'/n for n in selected]
        hashes[lane]={n:hashlib.sha256(p.read_bytes()).hexdigest() for n,p in zip(selected,objects)}
        cmd=[cc,*flags,*map(str,fixtures+objects),'-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--unresolved-symbols=ignore-all,--wrap=xv_preempt',
             '-lm','-lgcc','-o',str(out/(lane+'.elf'))]
        subprocess.run(cmd,check=True);commands.append(cmd)
    (out/'build.json').write_text(json.dumps(dict(commands=commands,retained_sha256=hashes),indent=2)+'\n')

class Callbacks:
    def step(self,u,address,size,user):
        if address==(self.symbols['__wrap_xv_preempt']&~1):
            def read(a,n):
                data=b''
                while n:
                    page=struct.unpack('<I',u.mem_read(PT+4*(a>>12),4))[0]
                    take=min(n,4096-(a&4095));data+=bytes(u.mem_read(RAM+page+(a&4095),take))
                    a+=take;n-=take
                return data
            self.callbacks.append(dict(bits=hashlib.sha256(read(0x30be10,512)).hexdigest(),
                selected=bytes(u.mem_read(RAM+0x38be10,2)).hex(),
                sp=bytes(u.mem_read(CTX+16,4)).hex(),
                budget=bytes(u.mem_read(CTX+self.layout['preempt'],4)).hex()))
        super().step(u,address,size,user)

class PassMachine(Callbacks,sp.Machine):
    def prepare(self,memory,context,spec):
        struct.pack_into('<I',memory,0x4000f8,4096)
        u=self.uc;u.reg_write(UC_ARM_REG_SP,STACK+65024);u.reg_write(UC_ARM_REG_LR,END|1)
        self.instructions=self.copies=self.copy_bytes=self.yields=0
        u.emu_start(self.symbols['portal_fixture_boot']|1,END,count=1000)
        assert u.reg_read(UC_ARM_REG_PC)==END
        u.mem_write(self.symbols['portal_test_decline'],struct.pack('<I',spec.get('decline',0)))
    def result(self,spec):
        self.callbacks=[]
        r=self.run_pass(spec,'f_00052E10')
        r['callbacks']=self.callbacks
        r['accepted']=list(struct.unpack('<2I',self.uc.mem_read(self.symbols['subcluster_accepted'],8))) if 'subcluster_accepted' in self.symbols else [0,0]
        return r

previous_fixture=pr.vp.fixture
def outer_fixture(layout,spec):
    memory,context,pages=previous_fixture(layout,spec);memory=bytearray(memory)
    def write(a,data):
        for i,b in enumerate(data):memory[pages[(a+i)>>12]+((a+i)&4095)]=b
    def word(a,v):write(a,struct.pack('<I',v))
    word(0x4000f8,4096);write(0x39cc15,b'\0');word(0x38be10,0)
    # Complete the camera used by the original 5BFD0/5C5E0 frustum builders.
    # The narrower portal fixture only initializes the projection subset.
    write(0x2feb90,struct.pack('<9f',0.,0.,0.,0.,0.,1.,0.,1.,0.))
    write(0x2febb8,struct.pack('<f4h',1.5707963267948966,0,0,960,544))
    write(0x2febcc,struct.pack('<2f',.1,100.))
    write(0x2e34b2,b'\0')
    write(0x1f0aa0,struct.pack('<f',.5));write(0x1f0b1c,struct.pack('<f',-.5))
    word(0x206f74,0x1eaec8);write(0x1eaec8,bytes(12))
    for i in range(spec['clusters']):
        sub=0x440000+i*0x100;word(0x401000+i*0x68+0x34,2);word(0x401000+i*0x68+0x38,sub)
        for j in range(2):
            box=[-.25,.25,-.25,.25,1.,2.] if j==0 else [100.,101.]*3
            write(sub+j*36,struct.pack('<6f',*box));word(sub+j*36+24,3);word(sub+j*36+28,0x450000+i*0x100+j*16)
            write(0x450000+i*0x100+j*16,struct.pack('<3I',i,i+31,j+100))
    return bytes(memory),context,pages

class OuterMachine(Callbacks,pr.Machine):
    def call(self,name,args=(),fpscr=0,active=False):
        if active and name=='f_000532E0':name='f_000539C0'
        return super().call(name,args,fpscr,active)
    def result(self,spec):
        self.callbacks=[]
        r=super().result(spec)
        r['callbacks']=self.callbacks
        r['subcluster_accepted']=list(struct.unpack('<2I',self.uc.mem_read(self.symbols['subcluster_accepted'],8))) if 'subcluster_accepted' in self.symbols else [0,0]
        r['subcluster_declined']=list(struct.unpack('<5I',self.uc.mem_read(self.symbols['subcluster_declined'],20))) if 'subcluster_declined' in self.symbols else []
        return r

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('reference','candidate','out'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--reuse',action='store_true');p.add_argument('--outer-only',action='store_true');a=p.parse_args();a.out=a.out.resolve()
    if a.out.is_relative_to(ROOT):p.error('private output required')
    a.out.mkdir(parents=True,exist_ok=a.reuse)
    if not a.reuse:build(a.reference.resolve(),a.candidate.resolve(),a.out)
    inside=[-.5,.5]*3;cross=[-.5,1.5]*3;outside=[3.,4.]*3
    clusters=[[(inside,[0,1,31,32,32]),(outside,[2,3]),(cross,[31,33,100])],[(cross,[100,200,511]),(inside,[]),(inside,[0,511,512])],[]]
    specs=[dict(name='empty',visible=[],clusters=[]),dict(name='no-subclusters',visible=[2],clusters=clusters),
           dict(name='ordered',visible=[0,1,2],clusters=clusters),dict(name='duplicate',visible=[1,0,1],clusters=clusters),
           dict(name='global',visible=[0,1],clusters=clusters,global_frustum=1),dict(name='preset',visible=[0,1],clusters=clusters,preset=[31,100,511],initial=3),
           dict(name='capacity',visible=[0,1],clusters=clusters,initial=16382),dict(name='full',visible=[0,1],clusters=clusters,initial=16384),
           dict(name='bulk',visible=[0],clusters=[[(inside,list(range(1024))),(inside,list(range(768,1792)))]]),
           dict(name='queued',visible=[0,1],clusters=clusters,decline=6,fallback=True),dict(name='budget',visible=[0,1],clusters=clusters,budget=1),
           dict(name='bulk-budget',visible=[0],clusters=[[(inside,list(range(1024))),(inside,list(range(768,1792)))]],budget=17)]
    for mode in range(16):specs.append(dict(name=f'fp-{mode}',visible=[0,1],clusters=clusters,mode=mode<<22))
    clockwise=[(-.5,-.5,1),(-.5,.5,1),(.5,.5,1),(.5,-.5,1)]
    for mode in (0,0x400000,0x800000,0xc00000,0x0300009f):
        specs.append(dict(name=f'outer-{mode:x}',outer=True,projection=True,clusters=4,edges=[(0,1),(1,2),(2,3)],portal_points=clockwise,budget=100000,fpscr=mode))
    specs.append(dict(name='outer-budget',outer=True,projection=True,clusters=4,edges=[(0,1),(1,2),(2,3)],portal_points=clockwise,budget=1,refill=17))
    if a.outer_only:specs=[s for s in specs if s.get('outer')]
    pr.vp.fixture=outer_fixture;rows=[]
    for s in specs:
        cls=OuterMachine if s.get('outer') else PassMachine
        lanes={n:cls(a.out/(n+'.elf')).result(s) for n in ('reference','candidate')}
        checks={k:lanes['reference'][k]==lanes['candidate'][k] for k in ('external','preserved','callbacks')}
        if s.get('fallback'):
            checks.update({k:lanes['reference'][k]==lanes['candidate'][k] for k in ('context','fpscr')})
        if s.get('outer'):
            for r in lanes.values():r['external_sha256']=hashlib.sha256(r.pop('external')).hexdigest()
        rows.append(dict(spec=s,checks=checks,lanes=lanes));(a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
        assert all(checks.values()),(s['name'],checks)
        cand=lanes['candidate'];accepted=cand.get('subcluster_accepted',cand.get('accepted'))
        if s['name'] not in ('empty','no-subclusters','full','queued'):assert sum(accepted)>0,(s['name'],cand)
        print('PASS',s['name'],'accepted',accepted,flush=True)
if __name__=='__main__':main()
