#!/usr/bin/env python3
"""ARM interval checks for stack/input aliases and split guest pages.

Complements the whole clip-region test. Reads the retained, unmodified helper;
generated test code and game-derived fragments stay in the private output.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from clip_distance_spans import INTERVALS, interval, FLAG
from test_visibility_outcode import Machine, SIZE, EDGES


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--stage',type=Path,required=True)
    ap.add_argument('--output-dir',type=Path,required=True)
    ap.add_argument('--cc',default='/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
    a=ap.parse_args();out=a.output_dir
    if out.resolve().is_relative_to(ROOT):raise ValueError('output must be private')
    out.mkdir(parents=True,exist_ok=False)
    source=(a.stage/'recomp/kernel/xk_clip_region.c').read_text()
    prefix=source[:source.index('/* Generated from the owned image')]
    units=[]
    for begin,end,copies,point,plane,uses,pin in INTERVALS:
        first=source.index('    /* '+begin+' ');last=source.index('    /* '+end+' ',first)
        body=source[first:last]
        if hashlib.sha256(body.encode()).hexdigest()!=pin or source.count(body)!=copies:
            raise ValueError('retained interval identity mismatch')
        body+='    /* '+end+' boundary */\n'
        changed=interval(body,begin,end,1,point,plane,uses,pin)
        load=''.join('uint32_t r%d=c->r[%d];'%(i,i) for i in range(8))
        load+=''.join('double s%d=c->st[%d];'%(i,i) for i in range(8))
        save=''.join('c->r[%d]=r%d;c->st[%d]=s%d;'%(i,i,i,i) for i in range(8))
        units.append('void cd_'+begin+'(xctx *restrict c){'+load+'\n'+changed+save+'}\n')
    (out/'intervals.c').write_text('#include "xv_x86rt.h"\n'+prefix+'\n'.join(units))
    flags=['-O2','-fno-strict-aliasing','-ffp-contract=off','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-std=gnu11','-I'+str(a.stage/'recomp')]
    commands=[]
    def run(cmd):commands.append(cmd);subprocess.run(cmd,check=True)
    run([a.cc,*flags,'-c',str(ROOT/'tools/tests/visibility_portal_arm.c'),'-o',str(out/'fixture.o')])
    for value in (0,1):
        obj=out/(str(value)+'.o');elf=out/(str(value)+'.elf')
        run([a.cc,*flags,'-D'+FLAG+'='+str(value),'-c',str(out/'intervals.c'),'-o',str(obj)])
        run([a.cc,*flags,str(out/'fixture.o'),str(obj),str(a.stage/'build/recomp/xv_x86rt.o'),'-nostdlib',
             '-Wl,-Ttext=0x10000,-e,test_boot,--unresolved-symbols=ignore-all,--wrap=xv_preempt','-lm','-lgcc','-o',str(elf)])
    machines=[Machine(out/(str(v)+'.elf')) for v in (0,1)];layout=machines[0].layout
    rows=[]
    for target in INTERVALS:
        name,_,_,pr,pl,_,_=target
        for seed in range(240):
            rng=random.Random(seed);memory=bytearray(SIZE);context=bytearray(rng.randbytes(layout['size']))
            pages=[(i^0x40)*4096 for i in range(SIZE//4096)]
            point,plane,sp=0x18000,0x28000,0x38000
            kind=seed%15
            if kind<4:point+=kind;plane+=kind
            elif kind==4:point+=4093
            elif kind==5:plane+=4091
            elif kind==6:point=plane
            elif kind==7:point=sp+0x14
            elif kind==8:point=sp+0x18
            elif kind==9:plane=sp+0x14
            elif kind==10:plane=sp+0x18
            elif kind==11:plane=sp+0x0c
            elif kind==12:point=sp+0x0a
            elif kind==13:pages[point>>12]=pages[sp>>12];point+=0x14
            else:pages[plane>>12]=pages[sp>>12];plane+=0x0c
            def word(address,value):
                for i,v in enumerate(struct.pack('<I',value)):
                    x=address+i;memory[pages[x>>12]+(x&4095)]=v
            for i in range(16):word(sp+i*4,rng.getrandbits(32))
            for i,addr in enumerate((point,point+4,plane,plane+4,plane+8)):
                value=EDGES[(seed+i*7)%len(EDGES)] if seed&1 else struct.unpack('<I',struct.pack('<f',rng.uniform(-8,8)))[0]
                word(addr,value)
            for i,v in ((int(pr[1]),point),(int(pl[1]),plane),(4,sp)):
                struct.pack_into('<I',context,4*i,v)
            for i in range(8):struct.pack_into('<d',context,layout['st']+8*i,(i-4)*.375)
            mode=seed//15;fpscr=(mode<<22)|0x9f
            fixture=(bytes(memory),bytes(context),pages,fpscr)
            ref=machines[0].run('cd_'+name,fixture);new=machines[1].run('cd_'+name,fixture)
            for key in ('context','memory','pages','fpscr'):
                if ref[key]!=new[key]:
                    (out/'failure.json').write_text(json.dumps(dict(target=name,seed=seed,key=key,reference=ref,actual=new),indent=2)+'\n')
                    raise AssertionError('interval mismatch: '+name+' '+str(seed)+' '+key)
            rows.append(dict(target=name,seed=seed,reference=ref['instructions'],candidate=new['instructions']))
    report=dict(result='PASS',cases=len(rows),commands=commands,rows=rows,
                scope='Exact interval state and memory; whole-function callbacks checked separately; not hardware cycles')
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',len(rows),'ARM interval cases')


if __name__=='__main__':main()
