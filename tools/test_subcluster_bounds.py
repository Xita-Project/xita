#!/usr/bin/env python3
"""Qualify a data-only subcluster classifier against retained Vita ARM code.

Finite ordered boxes, finite planes, original reverse-corner argument zero.
Compare classification and scheduling debit, not discarded register/scratch
state. No production hook, scheduling change or FPS measurement is made.
"""
import argparse
import hashlib
import json
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import test_arm_model_palette as base
base.SIZE=2<<20
from test_arm_model_palette import RAM,PT,CTX,STACK,END,SIZE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPSCR

def build(stage,out,xbe):
    from audit_visibility_dispatch import verify_image
    from recompiler.xita_recomp import Image
    proof=verify_image(xbe);img=Image(str(xbe))
    raw=img.bytes_at(0x5c300,0x2dd)
    assert hashlib.sha256(raw).hexdigest()=='5e463d77ea6ed255323f310d40cf3f7e847c08e7a6a71841b12545b1f937e1ab'
    cc=str(Path.home()/'vitasdk/bin/arm-vita-eabi-gcc')
    flags=['-O2','-std=gnu11','-fno-strict-aliasing','-ffp-contract=off','-frounding-math',
           '-mthumb','-mcpu=cortex-a9','-mfpu=neon','-ffunction-sections','-fdata-sections',
           '-I'+str(stage/'recomp')]
    commands=[];objects=[]
    sources=['tools/tests/visibility_portal_arm.c','tools/tests/subcluster_bounds_arm.c',
             'recomp/kernel/xk_subcluster_math.c']
    for i,name in enumerate(sources):
        p=out/f'fixture-{i}.o';cmd=[cc,*flags,'-c',str(ROOT/name),'-o',str(p)]
        subprocess.run(cmd,check=True);commands.append(cmd);objects.append(p)
    retained=[stage/'build/recomp'/name for name in ['code_010.o','xv_x86rt.o','kernel/xk_bounds.o']]
    elf=out/'bounds.elf'
    cmd=[cc,*flags,*map(str,objects+retained),'-nostdlib',
         '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,--unresolved-symbols=ignore-all',
         '-Wl,--undefined=f_0005C300,--undefined=xs_test_bounds,--undefined=layout',
         '-lm','-lgcc','-o',str(elf)]
    subprocess.run(cmd,check=True);commands.append(cmd)
    proof.update(commands=commands,retained_sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in retained},
                 source_sha256={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources})
    (out/'build.json').write_text(json.dumps(proof,indent=2)+'\n')
    return elf

class Machine(base.Machine):
    def __init__(self,elf):
        super().__init__(elf)
        self.imports={a:n for a,n in self.imports.items() if n not in ('getenv','atoi','__wrap_xv_preempt')}
        self.pages=[(i^1)*4096 for i in range(SIZE//4096)]
        self.uc.mem_write(PT,struct.pack('<'+'I'*len(self.pages),*self.pages))
    def write(self,a,data):
        while data:
            n=min(4096-(a&4095),len(data));self.uc.mem_write(RAM+self.pages[a>>12]+(a&4095),data[:n])
            a+=n;data=data[n:]
    def run(self,name,spec,mode):
        u=self.uc;f=0x12000+(0xffc if spec['split'] else 0);b=0x24000+(0xffc if spec['split'] else 0);sp=0x71000
        self.write(f,bytes(0x140));self.write(f+0x78,struct.pack('<16f',*sum(spec['planes'],[])))
        self.write(f+0x128,struct.pack('<6f',*spec['frustum_box']))
        self.write(b,struct.pack('<6f',*spec['box']));self.write(0x1f0a68,bytes(4));self.write(sp-128,bytes(136))
        self.write(sp,struct.pack('<II',0x52ec6,0))
        c=bytearray(self.layout['size'])
        for i in range(8):
            struct.pack_into('<I',c,i*4,0x15150000+i)
            struct.pack_into('<d',c,self.layout['st']+i*8,i+.375)
        for i,v in [(1,f),(4,sp),(7,b)]:struct.pack_into('<I',c,i*4,v)
        for n,v in [('fsp',spec['top']),('preempt',10000),('f_kind',3),('f_bits',32)]:struct.pack_into('<I',c,self.layout[n],v)
        struct.pack_into('<H',c,self.layout['fcw'],0x37f)
        u.mem_write(CTX,bytes(c));u.reg_write(UC_ARM_REG_R0,CTX);u.reg_write(UC_ARM_REG_SP,STACK+65024)
        u.reg_write(UC_ARM_REG_LR,END|1);u.reg_write(UC_ARM_REG_FPSCR,mode)
        self.instructions=self.copies=self.copy_bytes=self.yields=0
        u.emu_start(self.symbols[name]|1,END,count=100000)
        assert u.reg_read(UC_ARM_REG_PC)==END
        c=bytes(u.mem_read(CTX,len(c)))
        return dict(classification=struct.unpack_from('<H',c,0)[0],
                    budget=struct.unpack_from('<I',c,self.layout['preempt'])[0],
                    sp=struct.unpack_from('<I',c,16)[0],
                    instructions=self.instructions,copies=self.copies)

def fixtures():
    rng=random.Random(0x52ec1);rows=[]
    # Planes use independent coefficient signs and deliberate cancellation.
    # Explicit edge cases straddle exact/tiny positive plane distances.
    for i in range(192):
        box=[]
        scale=[2**-20,.25,1.,32.,4096.][i%5]
        for a in range(3):
            lo=rng.uniform(-2,2)*scale;hi=lo+rng.uniform(0,3)*scale
            box.extend([lo,hi])
        p=[[rng.uniform(-2,2) for _ in range(3)]+[rng.uniform(-3,3)*scale] for _ in range(4)]
        if i%4==0:p=[[1,0,0,scale],[-1,0,0,scale],[0,1,0,scale],[0,-1,0,scale]]
        if i%7==0:box=[-.5*scale,.5*scale]*3
        fb=[-65536,65536]*3
        if i%11==0:fb=[-scale,scale]*3
        rows.append(dict(name=f'random-{i}',box=box,planes=p,frustum_box=fb,split=i%2,top=i%8))
    for sign in (-1.,1.):
        for axis in range(3):
            for bits in (0x3effffff,0x3f000000,0x3f000001):
                distance=struct.unpack('<f',struct.pack('<I',bits))[0]
                p=[[0.,0.,0.,1.] for _ in range(4)]
                for i in range(4):p[i][axis]=sign;p[i][3]=distance
                rows.append(dict(name=f'plane-edge-{sign}-{axis}-{bits:x}',box=[-.5,.5]*3,
                                 planes=p,frustum_box=[-2,2]*3,split=True,top=3))
    for axis in range(3):
        for side in (0,1):
            for bits in (0x3f7fffff,0x3f800000,0x3f800001):
                edge=struct.unpack('<f',struct.pack('<I',bits))[0];box=[-.5,.5]*3
                box[2*axis:2*axis+2]=[edge,2.] if side else [-2.,-edge]
                rows.append(dict(name=f'aabb-edge-{axis}-{side}-{bits:x}',box=box,planes=[[0,0,0,1]]*4,
                                 frustum_box=[-1,1]*3,split=False,top=0))
    finite_bits=(0,0x80000000,1,0x007fffff,0x00800000,0x00800001,
                 0x33800000,0x3f7fffff,0x3f800000,0x3f800001,0x4b800000,0x7f7fffff)
    values=[struct.unpack('<f',struct.pack('<I',v))[0] for v in finite_bits]
    largest=values[-1]
    for i,v in enumerate(values):
        for j,w in enumerate(values):
            box=[-abs(v),abs(v),-abs(w),abs(w),0.,0.]
            p=[[v,-w,w,0.],[-v,w,-w,0.],[w,v,-v,0.],[-w,-v,v,0.]]
            rows.append(dict(name=f'finite-{i}-{j}',box=box,planes=p,
                             frustum_box=[-largest,largest]*3,split=True,top=i%8))
    return rows

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--xbe',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out=a.out.resolve()
    if a.out.is_relative_to(ROOT):p.error('private outputs must be outside source')
    a.out.mkdir(parents=True,exist_ok=False)
    elf=build(a.stage.resolve(),a.out,a.xbe.resolve());m=Machine(elf);cases=fixtures();rows=[]
    for mode in range(16):
        for s in cases:
            lanes={n:m.run(f,s,mode<<22) for n,f in [('original','f_0005C300'),('candidate','xs_test_bounds')]}
            ok=all(lanes['original'][k]==lanes['candidate'][k] for k in ('classification','budget','sp'))
            row=dict(name=s['name'],mode=mode,**lanes);rows.append(row)
            if not ok:
                (a.out/'failure.json').write_text(json.dumps(dict(spec=s,row=row),indent=2)+'\n')
                raise AssertionError(row)
        print('PASS mode',mode,len(cases),'cases',flush=True)
    result=dict(result='PASS',cases=len(rows),outcomes={str(k):sum(r['original']['classification']==k for r in rows) for k in (0,1,2)},
                rows=rows,limits=['Finite ordered boxes and finite plane coefficients; extra reverse-corner argument zero.',
                  'Classification, budget and stack return only. Owner admission, complete caller state and native scheduling remain unqualified.',
                  'Modeled ARM instructions are not hardware cycles or FPS.'])
    (a.out/'result.json').write_text(json.dumps(result,indent=2)+'\n')

if __name__=='__main__':main()
