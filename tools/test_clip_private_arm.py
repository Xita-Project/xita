#!/usr/bin/env python3
"""Vita ARM result validation using generated private-clip execution fixtures."""
from pathlib import Path
import argparse,subprocess,struct,math,json
import test_arm_model_palette as base
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPSCR
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();d=a.out.resolve();root=Path(__file__).resolve().parents[1]
cc=str(Path.home()/'vitasdk/bin/arm-vita-eabi-gcc')
flags=[cc,'-O2','-fno-strict-aliasing','-ffp-contract=off','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-ffunction-sections','-fdata-sections','-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_CLIP_PRIVATE=1','-DXV_NATIVE_CLIP_REGION','-I'+str(root/'recomp'),'-I'+str(d)]
objects=[]
for i,source in enumerate([root/'tools/tests/clip_private_arm.c',d/'reference.c',d/'region.c',d/'held.c',root/'recomp/xv_x86rt.c']):
 o=d/f'arm-{i}.o';subprocess.run(flags+['-c',str(source),'-o',str(o)],check=True);objects.append(str(o))
elf=d/'arm.elf';subprocess.run(flags+objects+['-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,--undefined=run_original,--undefined=run_held,--undefined=run_private,--undefined=layout','-lm','-lgcc','-o',str(elf)],check=True)
base.SIZE=2<<20
class Machine(base.Machine):
 def run_clip(self,name,memory,context,pages,fpscr):
  u=self.uc;u.mem_write(base.RAM,bytes(memory));u.mem_write(base.CTX,bytes(context));u.mem_write(base.PT,struct.pack('<512I',*pages));u.mem_write(base.STACK,bytes(65536));u.reg_write(UC_ARM_REG_R0,base.CTX);u.reg_write(UC_ARM_REG_SP,base.STACK+65024);u.reg_write(UC_ARM_REG_LR,base.END|1);u.reg_write(UC_ARM_REG_FPSCR,fpscr)
  self.instructions=self.copies=self.copy_bytes=self.yields=0
  u.emu_start(self.symbols[name]|1,base.END,count=2000000);assert u.reg_read(UC_ARM_REG_PC)==base.END
  return (bytes(u.mem_read(base.CTX,self.layout['size'])),bytes(u.mem_read(base.RAM,base.SIZE)),u.reg_read(UC_ARM_REG_FPSCR),struct.unpack('<I',u.mem_read(self.symbols['clip_private_released'],4))[0])
m=Machine(elf);m.imports={a:n for a,n in m.imports.items() if n!='getenv'};l=m.layout;rows=[]
for case in range(64):
 mem=bytearray(b'\xa5'*base.SIZE);ctx=bytearray(l['size']);pages=[i*4096 for i in range(512)]
 def word(a,v):struct.pack_into('<I',mem,a,v)
 def fp(a,v):struct.pack_into('<f',mem,a,v)
 n=3+case%30;edges=3+case%5;inp=0x81000;out=0x82000;clip=0x83000;sp=0xb8000
 if case%8==1:out=inp
 if case%8==2:inp=0x11000
 if case%8==3:out=0x12000
 for i in range(n):
  angle=math.tau*i/n;fp(inp+8*i,math.cos(angle)*(case%3+.25));fp(inp+8*i+4,math.sin(angle)*(case%3+.25))
 for i in range(edges):
  angle=(1 if case&1 else -1)*math.tau*i/edges;fp(clip+8*i,math.cos(angle));fp(clip+8*i+4,math.sin(angle))
 if case>=48:word(inp+4,[0x80000000,1,0x7f800000,0x7fc01234][case%4])
 fp(0x1f0a68,0);fp(0x1f0a78,1);struct.pack_into('<d',mem,0x1f0af8,9.999999747378752e-05)
 for i,v in enumerate([0x12345678,edges,clip,64,out]):word(sp+4*i,v)
 fp(sp+20,.0001)
 for i,v in [(4,sp),(1,n),(2,inp)]:struct.pack_into('<I',ctx,l['r']+4*i,v)
 struct.pack_into('<I',ctx,l['preempt'],1000000);struct.pack_into('<H',ctx,l['fcw'],0x37f);struct.pack_into('<I',ctx,l['fsp'],case%8)
 fpscr=((case//4)%4)<<22
 results=[m.run_clip(name,mem,ctx,pages,fpscr) for name in ['run_original','run_held','run_private']]
 assert results[0][:3]==results[1][:3]==results[2][:3],f'ARM mismatch case {case}'
 rows.append(dict(case=case,released=results[2][3],fpscr=fpscr))
 if case%16==15:print('passed',case+1,flush=True)
assert sum(r['released'] for r in rows)>0
(d/'arm-result.json').write_text(json.dumps(rows,indent=2)+'\n')
print('PASS: 64 ARM original/held/private context, memory and FPSCR cases; released',sum(r['released'] for r in rows))
