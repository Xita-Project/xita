#!/usr/bin/env python3
"""Keep original 52E10 publication/order; replace only its bounds call privately.

Compare the original retained ARM pass against a prepared experiment, including
all externally visible memory and exact budget. No production admission is
provided by this fixture and no game bodies are written inside the repository.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import test_subcluster_bounds as bounds
from test_subcluster_bounds import base,RAM,PT,CTX,STACK,END
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPSCR

SIZE=8<<20
base.SIZE=bounds.SIZE=SIZE

def build(stage,qualified,out,xbe):
    from audit_visibility_dispatch import verify_image
    from recompiler.xita_recomp import Image
    proof=verify_image(xbe);img=Image(str(xbe))
    assert hashlib.sha256(img.bytes_at(0x52e10,0x140)).hexdigest()=='8fad0bb52b35ae37187dc52f6be3fdde70c4fa9e4019b4ccc2d2982f9e1e7248'
    s=(stage/'recomp/code_009.c').read_text();a=s.index('void f_00052E10(');b=s.index('\nvoid f_',a+1);body=s[a:b]
    assert hashlib.sha256(body.encode()).hexdigest()=='75406ef2b4e74a0ef9431342e19f0853a8650faff04edf008a4ab984eafdfbbe'
    assert body.count('f_0005C300(c);')==1
    source=s[:s.index('\nvoid f_')]+'\nextern void xs_test_bounds(xctx *);\n'+body.replace('f_00052E10','xs_test_subcluster_pass').replace('f_0005C300(c);','xs_test_bounds(c);')
    assert source.count('L_00052ED7:')==1
    source=source.replace('extern void xs_test_bounds(xctx *);','extern void xs_test_bounds(xctx *);\nextern int xs_test_surface_union(xctx *);')
    source=source.replace('L_00052ED7:','L_00052ED7:\n    if (xs_test_surface_union(c)) goto L_00052F19;')
    (out/'pass.c').write_text(source)
    receipt=json.loads((qualified/'build.json').read_text());flags=receipt['commands'][0][:receipt['commands'][0].index('-c')]
    cmd=[*flags,'-c',str(out/'pass.c'),'-o',str(out/'pass.o')];subprocess.run(cmd,check=True)
    link=list(receipt['commands'][-1]);link[link.index('-o')+1]=str(out/'pass.elf')
    link.insert(link.index('-nostdlib'),str(out/'pass.o'));link.insert(link.index('-nostdlib'),str(stage/'build/recomp/code_009.o'))
    link.insert(-2,'-Wl,--undefined=f_00052E10,--undefined=xs_test_subcluster_pass')
    subprocess.run(link,check=True)
    proof.update(commands=[cmd,link],reference_caller_sha256=hashlib.sha256((stage/'build/recomp/code_009.o').read_bytes()).hexdigest())
    (out/'build.json').write_text(json.dumps(proof,indent=2)+'\n')

class Machine(bounds.Machine):
    def prepare(self,memory,context,spec):
        pass
    def run_pass(self,spec,name):
        memory=bytearray(SIZE);u=self.uc
        def put(a,fmt,*v):struct.pack_into('<'+fmt,memory,a,*v)
        def frustum(a,kind):
            planes=[[1,0,0,1],[-1,0,0,1],[0,1,0,1],[0,-1,0,1]]
            if kind==1:planes[0][3]=-.25
            put(a+0x78,'16f',*sum(planes,[]));put(a+0x128,'6f',*[-2,2]*3)
        put(0x30be0c,'h',len(spec['visible']));put(0x38be10,'h',spec.get('initial',0))
        put(0x39cc15,'B',spec.get('global_frustum',0));put(0x2fedc4,'i',0)
        put(0x400138,'I',0x410000);frustum(0x2febe4,1)
        cursor=0x450000
        for i,cluster in enumerate(spec['clusters']):
            sub=0x420000+i*0x1000;put(0x410000+i*0x68+0x34,'II',len(cluster),sub)
            for j,(box,faces) in enumerate(cluster):
                put(sub+j*36,'6f',*box);put(sub+j*36+24,'II',len(faces),cursor)
                for face in faces:put(cursor,'I',face);cursor+=4
        for i,cluster in enumerate(spec['visible']):
            a=0x2fee0c+i*0x1a0;put(a,'h',cluster);frustum(a+20,i%2)
        for face in spec.get('preset',[]):
            a=0x30be10+(face>>5)*4;v=struct.unpack_from('<I',memory,a)[0];put(a,'I',v|(1<<(face&31)))
        sp=0x780000;put(sp,'II',0x53af7,0x400000)
        c=bytearray(self.layout['size'])
        for i in range(8):
            struct.pack_into('<I',c,i*4,0x15150000+i)
            struct.pack_into('<d',c,self.layout['st']+i*8,i+.375)
        struct.pack_into('<I',c,16,sp)
        for key,v in [('fsp',3),('preempt',spec.get('budget',100000)),('f_kind',3),('f_bits',32)]:struct.pack_into('<I',c,self.layout[key],v)
        struct.pack_into('<H',c,self.layout['fcw'],0x37f)
        self.prepare(memory,c,spec)
        u.mem_write(RAM,bytes(memory));u.mem_write(CTX,bytes(c));u.mem_write(PT,struct.pack('<'+'I'*(SIZE//4096),*range(0,SIZE,4096)))
        u.reg_write(UC_ARM_REG_R0,CTX);u.reg_write(UC_ARM_REG_SP,STACK+65024);u.reg_write(UC_ARM_REG_LR,END|1)
        u.reg_write(UC_ARM_REG_FPSCR,spec.get('mode',0));self.instructions=self.copies=self.copy_bytes=self.yields=0
        u.emu_start(self.symbols[name]|1,END,count=20000000);assert u.reg_read(UC_ARM_REG_PC)==END
        result=bytes(u.mem_read(RAM,SIZE));context=bytes(u.mem_read(CTX,len(c)))
        # Original 52E10 and leaf scratch lies entirely below entry ESP.
        external=result[:sp-0x100]+result[sp:]
        preserved={f'r{i}':struct.unpack_from('<I',context,i*4)[0] for i in (3,4,5,6,7)}
        preserved.update({k:struct.unpack_from('<I',context,self.layout[k])[0] for k in ('fsp','preempt')})
        return dict(external=hashlib.sha256(external).hexdigest(),preserved=preserved,
                    selected=struct.unpack_from('<H',result,0x38be10)[0],instructions=self.instructions,
                    context=context.hex(),fpscr=u.reg_read(UC_ARM_REG_FPSCR))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('stage','qualified','out','xbe'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();a.out=a.out.resolve()
    if a.out.is_relative_to(bounds.ROOT):p.error('private output must stay outside source')
    a.out.mkdir(parents=True,exist_ok=False);build(a.stage.resolve(),a.qualified.resolve(),a.out,a.xbe.resolve())
    m=Machine(a.out/'pass.elf');inside=[-.5,.5]*3;crossing=[-.5,1.5]*3;outside=[3.,4.]*3
    clusters=[[(inside,[0,1,31,32,32]),(outside,[2,3]),(crossing,[31,33,100])],
              [(crossing,[100,200,511]),(inside,[]),(inside,[0,511,512])],[]]
    specs=[dict(name='empty',visible=[],clusters=[]),dict(name='no-subclusters',visible=[2],clusters=clusters),
           dict(name='ordered',visible=[0,1,2],clusters=clusters),dict(name='duplicate',visible=[1,0,1],clusters=clusters),
           dict(name='global-frustum',visible=[0,1],clusters=clusters,global_frustum=1),
           dict(name='preset',visible=[0,1],clusters=clusters,preset=[31,100,511],initial=3),
           dict(name='capacity',visible=[0,1],clusters=clusters,initial=16382),
           dict(name='capacity-full',visible=[0,1],clusters=clusters,initial=16384),
           dict(name='bulk',visible=[0],clusters=[[(inside,list(range(1024))),(inside,list(range(768,1792)))]])]
    for mode in range(16):specs.append(dict(name=f'fp-{mode}',visible=[0,1],clusters=clusters,mode=mode<<22))
    rows=[]
    for s in specs:
        lanes={n:m.run_pass(s,f) for n,f in [('original','f_00052E10'),('candidate','xs_test_subcluster_pass')]}
        checks={k:lanes['original'][k]==lanes['candidate'][k] for k in ('external','preserved')}
        rows.append(dict(name=s['name'],checks=checks,**lanes))
        (a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n');assert all(checks.values()),rows[-1]
        print('PASS',s['name'],lanes['original']['instructions'],lanes['candidate']['instructions'],flush=True)

if __name__=='__main__':main()
