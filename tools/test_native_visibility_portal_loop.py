#!/usr/bin/env python3
"""Private whole-532E0 fixture against exact retained Vita ARM objects.

Unresolved unused TU symbols remain unmapped and fail closed if reached.
Math/clip descendants execute; scheduler and idle-owner services are explicit
fixture imports. Instruction counts are not hardware cycles or FPS.
"""
import argparse
import bisect
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import sys
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import test_arm_model_palette as base
base.SIZE=8<<20
from test_arm_model_palette import RAM,PT,STACK,CTX,END,SIZE
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

CC='/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc'
ALT=0x28000000
OBSERVED=('f_000532E0','f_0001D130','f_000524B0','f_00053280','f_00052BE0',
 'f_000528D0','f_000B7F10','f_000117B0','f_000B71C0','f_000B5EA0','f_000B7B40',
 'xv_math_clip_region','xv_math_polygon_clip','x_str_movs','x_str_stos','__wrap_xv_preempt')
def sha(b):return hashlib.sha256(b).hexdigest()
def command(args):subprocess.run(args,check=True)

def build(stage,out):
    flags=['-O2','-fno-strict-aliasing','-ffp-contract=off','-mthumb','-mcpu=cortex-a9',
       '-mfpu=neon','-std=gnu11','-I'+str(stage/'recomp'),'-ffunction-sections','-fdata-sections']
    cmd=[CC,*flags,'-c',str(ROOT/'tools/tests/visibility_portal_arm.c'),'-o',str(out/'fixture.o')]
    command(cmd);commands=[cmd]
    names=['code_000.o','code_002.o','code_009.o','code_016.o','xv_x86rt.o',
       'kernel/xk_clip.o','kernel/xk_clip_region.o','kernel/xk_clip_region_control.o',
       'kernel/xk_math.o','kernel/xk_light_census.o']
    objects=[stage/'build/recomp'/n for n in names]
    elf=out/'reference.elf'
    cmd=[CC,*flags,str(out/'fixture.o'),*map(str,objects),'-nostdlib',
       '-Wl,-Ttext=0x10000,-e,test_boot,--unresolved-symbols=ignore-all,--wrap=xv_preempt',
       '-lm','-lgcc','-o',str(elf)]
    command(cmd);commands.append(cmd)
    source=(stage/'recomp/code_009.c').read_text()
    start=source.index('void f_000532E0(');end=source.index('\nvoid f_',start+1)
    body=source[start:end];prefix=source[:source.index('\nvoid f_')]
    from visibility_portal_loop import transform,FLAG
    private=(prefix+'\n'+transform(body,(stage/'recomp/xv_x86rt.h').read_text())).replace('f_000532E0','vp_candidate')
    (out/'candidate.c').write_text(private)
    for lane in ('off','candidate'):
        obj=out/(lane+'.o')
        cmd=[CC,*flags,'-DXV_EXPERIMENTAL_OBJECT_JOBS','-D'+FLAG+'='+str(int(lane=='candidate')),
             '-fstack-usage','-c',str(out/'candidate.c'),'-o',str(obj)]
        command(cmd);commands.append(cmd)
        cmd=[CC,*flags,str(out/'fixture.o'),str(obj),*map(str,objects),'-nostdlib',
          '-Wl,-Ttext=0x10000,-e,test_boot,--unresolved-symbols=ignore-all,--wrap=xv_preempt',
          '-lm','-lgcc','-o',str(out/(lane+'.elf'))]
        command(cmd);commands.append(cmd)
    (out/'build.json').write_text(json.dumps(dict(commands=commands,
       retained_objects={str(p):sha(p.read_bytes()) for p in objects},elf_sha256=sha(elf.read_bytes())),indent=2)+'\n')
    return elf

class Machine(base.Machine):
    def __init__(self,path):
        super().__init__(path)
        self.uc.mem_map(ALT,SIZE)
        self.active=False;self.instructions=0;self.profile=False
        self.imports={a:n for a,n in self.imports.items() if n not in ('getenv','atoi','__wrap_xv_preempt')}
        with path.open('rb') as f:
            elf=ELFFile(f)
            self.functions=sorted((s['st_value']&~1,s['st_size'],s.name) for s in
             elf.get_section_by_name('.symtab').iter_symbols() if s['st_info']['type']=='STT_FUNC' and s['st_size'])
        self.starts=[v[0] for v in self.functions]
        self.observers={self.symbols[n]&~1:n for n in OBSERVED if n in self.symbols}
        if 'vp_candidate' in self.symbols:self.observers[self.symbols['vp_candidate']&~1]='f_000532E0'
        self.uc.hook_add(UC_HOOK_MEM_WRITE,self.write)
    def word(self,a):return struct.unpack('<I',self.uc.mem_read(a,4))[0]
    def put(self,a,v):self.uc.mem_write(a,struct.pack('<I',v&0xffffffff))
    def snapshot(self):
        return dict(context=bytes(self.uc.mem_read(CTX,self.layout['size'])).hex(),
         memory=sha(bytes(self.uc.mem_read(RAM,SIZE))),pages=sha(bytes(self.uc.mem_read(PT,4<<20))),
         alternate_memory=sha(bytes(self.uc.mem_read(ALT,SIZE))),
         roots=[self.word(self.symbols[n]) for n in ('g_xram','g_xpt','g_img_base')],
         fpscr=self.uc.reg_read(UC_ARM_REG_FPSCR),yields=self.word(self.symbols['vp_yields']),
         probes=self.word(self.symbols['vp_stack_probes']))
    def write(self,u,access,address,size,value,user):
        if self.active:
            for area in (RAM,ALT):
                if area<=address and address+size<=area+SIZE:
                    self.writes.append((area,address-area,size,value&((1<<(8*size))-1)))
    def mutation(self):
        kind=self.case['mutation'];u=self.uc
        regs=list(struct.unpack('<8I',u.mem_read(CTX,32)));sp=regs[4]
        def addr(a):return RAM+self.word(PT+4*(a>>12))+(a&4095)
        if kind in ('root-replacement','table-remap'):
            if kind=='root-replacement':
                u.mem_write(ALT,bytes(u.mem_read(RAM,SIZE)))
                u.mem_write(PT+0x20000,bytes(u.mem_read(PT,8192)))
                for n,v in [('g_xram',ALT),('g_img_base',ALT),('g_xpt',PT+0x20000)]:self.put(self.symbols[n],v)
            # Parent table stays captured, but its entries are never frozen.
            page=0x402;destination=0x490000
            u.mem_write(RAM+destination,bytes(u.mem_read(RAM+self.word(PT+4*page),4096)))
            self.put(PT+4*page,destination)
        elif kind=='path-replacement':
            u.mem_write(RAM+0x408100,bytes(u.mem_read(RAM+0x408000,64)))
            self.put(RAM+0x2d2ba0,0x408100)
        elif kind=='signed-counter':
            # Change only at the genuine root53510 callback after its debit.
            # Next index is signed negative; an actual list entry at -1 is valid.
            self.put(addr(sp+0x18),0xffffffff);regs[0]=0xffffffff
            self.put(addr(regs[3]+0x5c),1)
            pointer=self.word(addr(regs[3]+0x60));u.mem_write(addr(pointer-2),struct.pack('<H',0))
        elif kind=='stale-flags':
            # Child inputs and guest traversal remain valid; high halves and
            # arbitrary lazy backing fields are observable at the next child.
            regs[0]=1;regs[1]=0xbadd0002;regs[2]=0xcafe1234
            u.mem_write(CTX+40,struct.pack('<9I',5,0x11112222,0x33334444,0x202,16,
                                         0x71727374,0x81828384,0x91929394,0xa1a2a3a4))
        else:raise AssertionError('unknown explicit mutation')
        u.mem_write(CTX,struct.pack('<8I',*regs))
        self.mutations+=1
    def step(self,u,address,size,user):
        if self.active:
            if address<0x10000:raise AssertionError('unresolved retained symbol reached: '+hex(address))
            self.min_sp=min(self.min_sp,u.reg_read(UC_ARM_REG_SP))
            if self.profile:
                i=bisect.bisect_right(self.starts,address)-1
                start,length,name=self.functions[i] if i>=0 else (0,0,'unknown')
                self.by_function[name if start<=address<start+length else 'unknown']+=1
                self.by_pc[address]+=1
            while self.returns and self.returns[-1][1:]==(address,u.reg_read(UC_ARM_REG_SP)):
                name,_,_=self.returns.pop()
                self.records.append(dict(event='return',name=name,state=self.snapshot()))
                if name=='__wrap_xv_preempt' and self.mutate_pending:
                    self.mutate_pending=False;self.mutation()
                    self.records.append(dict(event='callback-mutation',name=self.case['mutation'],state=self.snapshot()))
            name=self.observers.get(address)
            if name:
                if u.reg_read(UC_ARM_REG_R0)!=CTX:raise AssertionError('original context identity changed')
                if name=='__wrap_xv_preempt' and self.case.get('mutation') and not self.mutations:
                    parents=[f[0] for f in self.returns]
                    wanted=['f_000532E0']+(['f_000524B0'] if self.case.get('frontier')=='bounds' else [])
                    if parents==wanted:
                        r=struct.unpack('<8I',u.mem_read(CTX,32))
                        # Validate actual root53510, never invent a loop entry.
                        local=self.word(RAM+self.word(PT+4*((r[4]+0x18)>>12))+((r[4]+0x18)&4095))
                        signed=(local&65535);signed=signed if signed<32768 else signed-65536
                        if self.case.get('frontier')!='bounds' and (r[0]!=(signed&0xffffffff) or self.word(CTX+40)!=3):
                            raise AssertionError('unexpected original portal callback state')
                        self.mutate_pending=True
                self.records.append(dict(event='entry',name=name,state=self.snapshot()))
                self.calls[name]+=1
                self.returns.append((name,u.reg_read(UC_ARM_REG_LR)&~1,u.reg_read(UC_ARM_REG_SP)))
                self.max_depth=max(self.max_depth,sum(f[0]=='f_000532E0' for f in self.returns))
            # Model firmware/libc copies explicitly and include their writes.
            imp=self.imports.get(address)
            if imp in ('memcpy','memmove','memset'):
                dst,src,n=(u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2))
                for area in (RAM,ALT):
                    if area<=dst and dst+n<=area+SIZE:
                        data=bytes([src&255])*n if imp=='memset' else bytes(u.mem_read(src,n))
                        self.writes.extend((area,dst+i-area,1,v) for i,v in enumerate(data))
        super().step(u,address,size,user)
    def call(self,name,args=(),fpscr=0,active=False):
        u=self.uc
        for r,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2),args):u.reg_write(r,v)
        u.reg_write(UC_ARM_REG_SP,STACK+65024);u.reg_write(UC_ARM_REG_LR,END|1);u.reg_write(UC_ARM_REG_FPSCR,fpscr)
        self.instructions=self.copies=self.copy_bytes=self.yields=0
        self.active=active;self.records=[];self.writes=[];self.calls=Counter();self.returns=[]
        self.mutations=0;self.mutate_pending=False
        self.max_depth=0
        self.by_function=Counter();self.by_pc=Counter();self.min_sp=0xffffffff
        try:u.emu_start(self.symbols[name]|1,END,count=3000000)
        except Exception:
            print('EXECUTION FAILURE',name,hex(u.reg_read(UC_ARM_REG_PC)),hex(u.reg_read(UC_ARM_REG_LR)),
              dict(self.calls),list(self.by_pc.items())[-8:],flush=True)
            raise
        if u.reg_read(UC_ARM_REG_PC)!=END:
            print('CEILING',hex(u.reg_read(UC_ARM_REG_PC)),dict(self.calls),self.by_function.most_common(6),
              struct.unpack('<8I',u.mem_read(CTX,32)),flush=True)
            raise AssertionError('instruction ceiling, not completion')
        if active:
            while self.returns and self.returns[-1][1:]==(END,u.reg_read(UC_ARM_REG_SP)):
                n,_,_=self.returns.pop();self.records.append(dict(event='return',name=n,state=self.snapshot()))
            if self.returns:raise AssertionError('unmatched actual child return frontiers')
        self.active=False
        return dict(instructions=self.instructions,by_function=dict(self.by_function),
          full_pc_histogram={hex(k):v for k,v in sorted(self.by_pc.items())},
          peak_native_stack=STACK+65024-self.min_sp,calls=dict(self.calls),
          copies=self.copies,copy_bytes=self.copy_bytes,observations=len(self.records),writes=len(self.writes),mutations=self.mutations,
          max_recursion_depth=self.max_depth)
    def run(self,spec,profile=True):
        self.profile=profile;self.case=spec
        self.call('xv_native_clip_region_init')
        self.call('xv_native_clip_region_override',(spec.get('native_clip',1),))
        memory,context,pages=fixture(self.layout,spec)
        self.uc.mem_write(RAM,memory);self.uc.mem_write(ALT,bytes(SIZE));self.uc.mem_write(CTX,context);self.uc.mem_write(PT,bytes(4<<20))
        self.uc.mem_write(PT,struct.pack('<'+'I'*len(pages),*pages));self.uc.mem_write(STACK,bytes(65536))
        for n,v in [('g_xram',RAM),('g_img_base',RAM),('g_xpt',PT),('vp_yields',0),('vp_stack_probes',0),('vp_refill',spec.get('refill',10000))]:self.put(self.symbols[n],v)
        stats=self.call('vp_candidate' if 'vp_candidate' in self.symbols else 'f_000532E0',(CTX,),spec.get('fpscr',0),True)
        if spec.get('mutation') and self.mutations!=1:raise AssertionError('required original callback mutation not executed')
        if spec.get('expect_depth') and self.max_depth!=spec['expect_depth']:raise AssertionError('required graph depth not exercised')
        return dict(spec=spec,stats=stats,final=self.snapshot(),records=self.records,writes=self.writes)

def fixture(layout,spec):
    memory=bytearray(b'\xa5'*SIZE);context=bytearray(b'\x5a'*layout['size'])
    pages=[i*4096 for i in range(SIZE//4096)]
    def raw(a,data):memory[a:a+len(data)]=data
    def word(a,v):raw(a,struct.pack('<I',v&0xffffffff))
    def half(a,v):raw(a,struct.pack('<H',v&65535))
    def fp(a,v):raw(a,struct.pack('<f',v))
    def reg(i,v):struct.pack_into('<I',context,4*i,v&0xffffffff)
    def field(n,v,fmt='I'):struct.pack_into('<'+fmt,context,layout[n],v)
    edges=spec.get('edges',[(0,1)]);n=spec.get('clusters',max((max(e) for e in edges),default=0)+1)
    for a in (0x2fedcc,0x408000):raw(a,bytes(256))
    for a,v in [(0x39be58,0x400000),(0x2fedc4,0),(0x2d2ba0,0x408000),(0x206fc0,0x409000),
       (0x400134,n),(0x400138,0x401000),(0x40014c,0x404000),(0x400158,0x403000),
       (0x4000b4,0x405000),(0x405010,0x406000)]:word(a,v)
    half(0x30be0c,0)
    for j,v in enumerate((3.402823466e38,-3.402823466e38,3.402823466e38,-3.402823466e38)):fp(0x409000+4*j,v)
    for i in range(n):
        adjacent=[j for j,e in enumerate(edges) if i in e]
        lists=0x420000 if n>16 else 0x402000
        word(0x401000+0x68*i+0x5c,len(adjacent));word(0x401000+0x68*i+0x60,lists+0x100*i)
        for j,e in enumerate(adjacent):half(lists+0x100*i+2*j,e)
        word(0x404000+4*i,spec.get('pvs',(1<<n)-1))
    for i,(a,b) in enumerate(edges):
        p=0x403000+0x40*i;half(p,a);half(p+2,b);word(p+4,0);half(p+0x34,0);word(p+0x38,0x407000)
    for j,v in enumerate((1,0,0,0)):fp(0x406000+4*j,v)
    for j in range(3):fp(0x2feb90+4*j,0)
    raw(0x2febb4,b'\0');raw(0x1f0e90,struct.pack('<d',1e-6))
    if spec.get('projection'):
        for j,v in enumerate((0,0,1,spec.get('plane_distance',1))):fp(0x406000+4*j,v)
        # Identity scaled affine transform at the classifier's actual input.
        matrix=0x2febf4
        for j in range(13):fp(matrix+4*j,1 if j in (0,1,5,9) else 0)
        for j,v in enumerate((0,0,1,0)):fp(0x1e0a0c+4*j,v)
        for j,v in enumerate((0,0,1)):fp(0x2feb9c+4*j,v)
        fp(0x1f0a68,0);fp(0x1f0abc,1);fp(0x1f0a78,1)
        raw(0x1f0af8,struct.pack('<d',9.999999747378752e-5))
        vertices=spec.get('portal_points',[(-.5,-.5,1),(.5,-.5,1),(.5,.5,1),(-.5,.5,1)])
        vertices_base=spec.get('portal_vertex_base',0x407000)
        for i in range(len(edges)):
            p=0x403000+0x40*i;half(p+0x34,len(vertices));word(p+0x38,vertices_base+0x100*i)
            for j,v in enumerate(vertices):
                for k,x in enumerate(v):fp(vertices_base+0x100*i+12*j+4*k,x)
        raw(0x2fedc9,bytes([spec.get('skip_distance',1)]));fp(0x2febd0,spec.get('distance_threshold',0))
    poly_count=spec.get('poly_count',4);half(0x410000,poly_count)
    points=[(-1,-1),(1,-1),(1,1),(-1,1)] if poly_count==4 else [
       (.25*math.cos(2*math.pi*i/poly_count),.25*math.sin(2*math.pi*i/poly_count)) for i in range(poly_count)]
    for j,v in enumerate(points):
        for k,x in enumerate(v):fp(0x410004+8*j+4*k,x)
    word(0x780000,0xdecafbad)
    for j in range(8):reg(j,0x51510000+j);struct.pack_into('<d',context,layout['st']+j*8,j+.375)
    reg(1,0);reg(2,0x410000);reg(4,0x780000)
    field('fsp',0);field('fsw',0xabcd,'H');field('fcw',0x27f,'H');field('preempt',spec.get('budget',10000))
    field('f_kind',5);field('f_bits',32)
    struct.pack_into('<I',context,36,0) # DF; no changed string semantics.
    if spec.get('portal_count') is not None:word(0x40105c,spec['portal_count'])
    if spec.get('alias')=='path-side':
        pages[0x408]=0x77e000;word(0x2d2ba0,0x408ff0);word(0x77eff0,0)
    if spec.get('alias')=='record-poly':
        pages[0x410]=0x2fe000;reg(2,0x410e0c);half(0x2fee0c,4)
    if spec.get('alias')=='list-count':
        pages[0x402]=0x401000;word(0x401060,0x40205c);word(0x40105c,1)
        word(0x401000+0x68+0x5c,0)
    if spec.get('noncontiguous'):
        original=bytes(memory)
        for i in range(0x400,len(pages)):
            pages[i]=(i^1)*4096;memory[pages[i]:pages[i]+4096]=original[i*4096:(i+1)*4096]
    return bytes(memory),bytes(context),pages

def negative_controls(directory,out):
    source=(directory/'candidate.c').read_text()
    commands=json.loads((directory/'build.json').read_text())['commands']
    base_compile=commands[-2];base_link=commands[-1]
    controls=[
      ('wrong-live-roots','    VP_LOAD();\n',
       '    uint8_t *const xram_=g_xram,*const imgb_=g_img_base; const uint32_t *const xpt_=g_xpt;\n    VP_LOAD();\n',
       dict(name='root-before-region',mutation='root-replacement',frontier='bounds',budget=1,refill=1)),
      ('widen-side-store','X_M8((c->r[4]+0x28u)) = VP_R8L(0);',
       'X_M32((c->r[4]+0x28u)) = VP_R8L(0);',dict(name='two-cluster')),
      ('drop-callback-reload','xv_preempt(c); VP_LOAD();','xv_preempt(c);',
       dict(name='signed-counter',mutation='signed-counter',edges=[(0,1),(0,1)],budget=1,refill=1)),
      ('double-budget','if(--c->preempt<=0)','if((c->preempt-=2)<=0)',
       dict(name='callbacks',edges=[(0,1),(0,1)],budget=1,refill=1))]
    rows=[];executed=[]
    for name,before,after,spec in controls:
        if source.count(before)!=1:raise AssertionError('negative control literal changed: '+name)
        path=out/(name+'.c');path.write_text(source.replace(before,after,1))
        cmd=list(base_compile);cmd[cmd.index('-c')+1]=str(path);cmd[cmd.index('-o')+1]=str(out/(name+'.o'))
        command(cmd);executed.append(cmd)
        cmd=list(base_link);cmd[cmd.index(str(directory/'candidate.o'))]=str(out/(name+'.o'))
        cmd[-1]=str(out/(name+'.elf'));command(cmd);executed.append(cmd)
        reference=Machine(directory/'reference.elf').run(spec,False)
        wrong=Machine(out/(name+'.elf')).run(spec,False)
        checks={k:reference[k]==wrong[k] for k in ('final','records','writes')}
        if all(checks.values()):raise AssertionError('negative control was not rejected: '+name)
        first=next((i for i,(a,b) in enumerate(zip(reference['records'],wrong['records'])) if a!=b),None)
        rows.append(dict(name=name,rejected=True,checks=checks,first_observer_difference=first,
          reference_observers=len(reference['records']),wrong_observers=len(wrong['records']),
          elf_sha256=sha((out/(name+'.elf')).read_bytes())))
        (out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
        print('REJECTED negative control',name,checks,flush=True)
    (out/'commands.json').write_text(json.dumps(executed,indent=2)+'\n')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--reuse',action='store_true')
    p.add_argument('--case-prefix',action='append',default=[])
    p.add_argument('--negative-controls',type=Path)
    p.add_argument('--explore-capacity',action='store_true',help=
       'Include the unqualified clipping-capacity exploration; the retained '
       'reference currently reaches the instruction ceiling in this case.')
    a=p.parse_args();a.out=a.out.resolve();a.stage=a.stage.resolve()
    if a.out.is_relative_to(ROOT):p.error('owned outputs must stay outside source')
    a.out.mkdir(parents=True,exist_ok=a.reuse)
    if a.negative_controls:return negative_controls(a.negative_controls.resolve(),a.out)
    elf=a.out/'reference.elf' if a.reuse else build(a.stage,a.out)
    specs=[dict(name='leaf',clusters=1,edges=[]),dict(name='two-cluster'),
       dict(name='parallel-portals',edges=[(0,1),(0,1)]),
       dict(name='cycle',edges=[(0,1),(1,2),(2,0)]),
       dict(name='diamond',edges=[(0,1),(0,2),(1,3),(2,3)]),
       dict(name='pvs-skip',pvs=1),dict(name='callbacks',edges=[(0,1),(0,1)],budget=1,refill=1),
       dict(name='projection',projection=True),dict(name='projection-region-off',projection=True,native_clip=0),
       dict(name='projection-reject',projection=True,plane_distance=-1),
       dict(name='distance-pass',projection=True,skip_distance=0,distance_threshold=2),
       dict(name='distance-reject',projection=True,skip_distance=0,distance_threshold=0),
       dict(name='clip-empty',projection=True,portal_points=[(2,2,1),(3,2,1),(3,3,1),(2,3,1)]),
       dict(name='projection-callbacks',projection=True,budget=1,refill=1),
       dict(name='projection-generic-callbacks',projection=True,native_clip=0,budget=1,refill=1),
       dict(name='negative-portal-count',portal_count=0xffffffff),
       dict(name='alias-path-side',alias='path-side'),dict(name='noncontiguous-pages',noncontiguous=True),
       dict(name='alias-record-poly',alias='record-poly'),
       dict(name='alias-list-count',alias='list-count',edges=[(0,1),(0,1)]),
       dict(name='negative-target',clusters=2,edges=[(0,65535)]),
       dict(name='out-of-range-target',clusters=2,edges=[(0,2)]),
       dict(name='empty-polygon',poly_count=0),dict(name='triangle-polygon',poly_count=3)]
    if a.explore_capacity:
        specs.append(dict(name='clip-overflow',projection=True,poly_count=256,
           portal_points=[(-1,-1,1),(.24995,-1,1),(.24995,1,1),(-1,1,1)]))
    for mutation in ('root-replacement','table-remap','path-replacement','signed-counter','stale-flags'):
        specs.append(dict(name=mutation,mutation=mutation,edges=[(0,1),(0,1)],budget=1,refill=1))
    specs.append(dict(name='root-before-region',mutation='root-replacement',frontier='bounds',budget=1,refill=1))
    specs.append(dict(name='depth32',clusters=32,edges=[(i,i+1) for i in range(31)],expect_depth=32))
    for fpscr in (0x400000,0x800000,0xc00000,0x0300009f):
        specs.append(dict(name=f'projection-fpscr-{fpscr:x}',projection=True,fpscr=fpscr,budget=1,refill=1))
    if a.case_prefix:specs=[s for s in specs if any(s['name'].startswith(p) for p in a.case_prefix)]
    rows=[]
    for spec in specs:
        lanes={};expected=None
        for lane in ('reference','off','candidate'):
            m=Machine(a.out/(lane+'.elf'))
            try:row=m.run(spec)
            except Exception as exc:
                (a.out/'execution-failure.json').write_text(json.dumps(dict(
                  spec=spec,lane=lane,error=str(exc),qualified=False),indent=2)+'\n')
                raise
            if expected is None:expected=row
            checks={k:row[k]==expected[k] for k in ('final','records','writes')}
            row['checks']=checks;lanes[lane]=row
            if not all(checks.values()):
                (a.out/'failure.json').write_text(json.dumps(dict(spec=spec,lane=lane,lanes=lanes),indent=2)+'\n')
                raise AssertionError((spec['name'],lane,checks))
        (a.out/(spec['name']+'.json')).write_text(json.dumps(lanes,indent=2)+'\n')
        rows.append(dict(spec=spec,lanes={k:dict(stats=v['stats'],final=v['final'],checks=v['checks']) for k,v in lanes.items()}))
        (a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
        print('PASS',spec['name'],{k:v['stats']['instructions'] for k,v in lanes.items()},flush=True)
    print('Completed exact whole-call comparisons; no hardware FPS claim.')
if __name__=='__main__':main()
