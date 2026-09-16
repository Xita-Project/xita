#!/usr/bin/env python3
"""VitaSDK ARM numeric/dirty-state oracle; not a Vita scheduler/device test."""
from pathlib import Path
import argparse,bisect,json,os,struct,subprocess
from collections import Counter
from elftools.elf.elffile import ELFFile
import test_arm_model_palette as base
base.SIZE=8<<20
from test_arm_model_palette import Machine,RAM,PT,STACK,CTX,END,SIZE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPSCR
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--quick',action='store_true');p.add_argument('--no-probe',action='store_true',help='measure the production adapter with no readiness test callback');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
cc=os.environ.get('ARM_CC','/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
flags=['-O2','-g','-std=gnu11','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_WORKER_QUERY','-I'+str(ROOT/'recomp')]
if not a.no_probe:flags+=['-DXV_WORKER_QUERY_TEST']
commands=[];objects=[]
for i,path in enumerate((a.reference,ROOT/'tools/tests/worker_query_arm.c',ROOT/'recomp/kernel/xk_worker_query.c',ROOT/'recomp/xv_x86rt.c')):
 obj=a.out/f'unit-{i}.o';extra=['-ffp-contract=off','-frounding-math'] if path.name=='xk_worker_query.c' else []
 cmd=[cc,*flags,*extra,'-c',str(path),'-o',str(obj)];subprocess.run(cmd,check=True);commands.append(cmd);objects.append(str(obj))
sdk=Path(cc).resolve().parents[1]
jump=a.out/'sdk-setjmp.o';jump.write_bytes(subprocess.check_output([str(sdk/'bin/arm-vita-eabi-ar'),'p',str(sdk/'arm-vita-eabi/lib/libc.a'),'lib_a-setjmp.o']))
elf=a.out/'query.elf';cmd=[cc,*flags,*objects,str(jump),'-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,--undefined=run_original,--undefined=run_candidate,--undefined=layout','-lm','-lgcc','-o',str(elf)]
subprocess.run(cmd,check=True);commands.append(cmd);(a.out/'commands.json').write_text(json.dumps(commands,indent=2))
class QueryMachine(Machine):
 def __init__(self,path):
  super().__init__(path);self.imports[self.symbols['memcmp']&~1]='memcmp'
  self.uc.mem_write(self.symbols['g_img_base'],struct.pack('<I',RAM+(4<<20)))
  with path.open('rb') as f:
   elf=ELFFile(f)
   self.functions=sorted((s['st_value']&~1,s['st_size'],s.name) for s in elf.get_section_by_name('.symtab').iter_symbols() if s['st_info']['type']=='STT_FUNC' and s['st_size'])
  self.starts=[s[0] for s in self.functions]
 def step(self,u,address,size,user):
  i=bisect.bisect_right(self.starts,address)-1
  start,length,name=self.functions[i] if i>=0 else (0,0,'unknown')
  if not start<=address<start+length:name='unknown'
  self.by_function[name]+=1
  if self.imports.get(address)=='getenv':
   self.instructions+=1;u.reg_write(UC_ARM_REG_R0,0);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR));return
  if self.imports.get(address)=='memcmp':
   self.instructions+=1
   left,right,n=[u.reg_read(reg) for reg in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)]
   b,c=bytes(u.mem_read(left,n)),bytes(u.mem_read(right,n));self.compared+=n
   u.reg_write(UC_ARM_REG_R0,0 if b==c else 1);u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR));return
  return super().step(u,address,size,user)
 def run(self,function,fixture):
  memory,context,pages,fpscr=fixture;u=self.uc
  u.mem_write(RAM,memory);u.mem_write(CTX,context);u.mem_write(PT,struct.pack('<'+'I'*len(pages),*pages));u.mem_write(STACK,bytes(65536))
  u.reg_write(UC_ARM_REG_R0,CTX);u.reg_write(UC_ARM_REG_SP,STACK+65024);u.reg_write(UC_ARM_REG_LR,END|1);u.reg_write(UC_ARM_REG_FPSCR,fpscr)
  self.instructions=self.copies=self.copy_bytes=self.yields=self.compared=0
  self.by_function=Counter()
  # Read existing lane-zero production counters out of band; this adds no ARM
  # instruction or callback. Stats begins with uint64_t attempts, applied.
  before=struct.unpack('<Q',u.mem_read(self.symbols['stats']+8,8))[0]
  u.emu_start(self.symbols[function]|1,END,count=20000000);assert u.reg_read(UC_ARM_REG_PC)==END
  applied=struct.unpack('<Q',u.mem_read(self.symbols['stats']+8,8))[0]-before
  assert sum(self.by_function.values())==self.instructions
  return dict(context=bytes(u.mem_read(CTX,self.layout['size'])),memory=bytes(u.mem_read(RAM,SIZE)),fpscr=u.reg_read(UC_ARM_REG_FPSCR),instructions=self.instructions,copy_bytes=self.copy_bytes,compare_bytes=self.compared,yields=self.yields,admitted=applied,by_function=dict(self.by_function))
m=QueryMachine(elf)
axes=bytes(map(int,(ROOT/'recomp/kernel/worker_query_axes.h').read_text().split('{')[1].split('}')[0].split(',')))
def fixture(n,case,fpscr):
 mem=bytearray(SIZE);ctx=bytearray(m.layout['size']);pages=[(i^1)*4096 for i in range(1024)]
 def put(addr,data):
  for j,b in enumerate(data):mem[pages[(addr+j)>>12]+((addr+j)&4095)]=b
 def word(addr,x):put(addr,struct.pack('<I',x&0xffffffff))
 def half(addr,x):put(addr,struct.pack('<H',x&65535))
 def fp(addr,x):put(addr,struct.pack('<f',x))
 def image(addr,x):struct.pack_into('<I',mem,(4<<20)+addr,x)
 image(0x39be58,0x10000);image(0x39be50,0x11000);word(0x100b4,0x11000);word(0x11010,0x12000)
 for addr,x in ((0x10134,n),(0x10138,0x20000),(0x10154,n-1),(0x10158,0x50000)):word(addr,x)
 put(0x12000,struct.pack('<4f',0,0,1,0));put(0x1eaf30,axes);fp(0x1f0a68,0)
 for k in range(n):
  adj=[]
  if k:adj.append(k-1)
  if k+1<n:adj.append(k)
  word(0x20000+k*104+0x5c,len(adj));word(0x20000+k*104+0x60,0x40000+k*64)
  for j,index in enumerate(adj):half(0x40000+k*64+2*j,index)
  word(0x2d2fb0+4*k,0x12340000+k);word(0x91000+4*k,0xffffffff)
  if k+1==n:continue
  portal=0x50000+k*64;half(portal,k);half(portal+2,k+1);put(portal+8,struct.pack('<4f',0,0,0,100));word(portal+0x34,4);word(portal+0x38,0x70000+k*128)
  put(0x70000+k*128,struct.pack('<12f',-10,-10,0,10,-10,0,10,10,0,-10,10,0))
 image(0x2d2fac,0xffffffff if case==11 else 100)
 for addr,x in ((0x90000,0x91000),(0x90004,0x92000),(0x90008,0x93000)):word(addr,x)
 for pool,nodes in ((0x92000,0x94000),(0x93000,0xa4000)):
  half(pool+0x20,0 if case==10 else 1024);half(pool+0x22,12);half(pool+0x32,0x8001);word(pool+0x34,nodes)
 word(0xb4000,0xffffffff);sp=0x1e0000
 radii=[0x42c80000,0,0xbf800000,0x7f800000,0x7fc12345,0x7f801234,1,0x80000001,0x7f7fffff]
 for addr,x in ((sp,0x925b0),(sp+4,0x80000000),(sp+8,0xb4000),(sp+12,sp+80),(sp+16,radii[case] if case<len(radii) else radii[0])):word(addr,x)
 half(sp+68,0xffff if case==9 else 0)
 if case==12:word(0x70000,0x7f801234)
 if case==13:word(0x70000,0x7f7fffff)
 if case==14:pages[(sp-4096)>>12]=pages[0x70000>>12]
 for i in range(8):
  struct.pack_into('<I',ctx,m.layout['r']+4*i,0x13570000+i*13)
  struct.pack_into('<Q',ctx,m.layout['st']+8*i,0x7ff8000000001234+i if case&1 else 0x3ff0000000000000+i)
 for i,x in ((0,sp+64),(4,sp),(7,0x90000)):struct.pack_into('<I',ctx,m.layout['r']+4*i,x)
 for field,x in (('fsp',case&7),('preempt',1 if case==15 else 1000000),('f_kind',3),('f_bits',32)):struct.pack_into('<I',ctx,m.layout[field],x)
 struct.pack_into('<H',ctx,m.layout['fsw'],(case*0x9123)&65535);struct.pack_into('<H',ctx,m.layout['fcw'],0x37f)
 # Standalone DF=1 cases remain part of exact state comparison; no subsequent
 # allocator reuse is attempted on those deliberately arbitrary contexts.
 struct.pack_into('<I',ctx,36,case&1)
 return bytes(mem),bytes(ctx),pages,fpscr
rows=[]
for rounding in range(1 if a.quick else 4):
 for control in ((0,) if a.quick else (0,0x10,0x01000000,0x02000000,0x0300009f)):
  for n in ((7,) if a.quick else (1,7,65)):
   for case in range(16):
    sample=fixture(n,case,(rounding<<22)|control);original=m.run('run_original',sample);candidate=m.run('run_candidate',sample)
    record=dict(rounding=rounding,control=control,n=n,case=case)
    for field in ('context','memory','fpscr','yields'):
     if original[field]!=candidate[field]:
      (a.out/'failure.json').write_text(json.dumps(dict(record,field=field,original=str(original[field])[:300] if isinstance(original[field],int) else 'binary',candidate=str(candidate[field])[:300] if isinstance(candidate[field],int) else 'binary')))
      if isinstance(original[field],bytes):
       first=next(i for i,(x,y) in enumerate(zip(original[field],candidate[field])) if x!=y);raise AssertionError((record,field,hex(first),original[field][first:first+16].hex(),candidate[field][first:first+16].hex()))
      raise AssertionError((record,field,original[field],candidate[field]))
    record.update(original={k:v for k,v in original.items() if k not in ('context','memory')},candidate={k:v for k,v in candidate.items() if k not in ('context','memory')});rows.append(record)
   print('PASS ARM',rounding,hex(control),n,flush=True)
(a.out/'result.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'ARM full context/memory/FPSCR comparisons; imports counted separately')
