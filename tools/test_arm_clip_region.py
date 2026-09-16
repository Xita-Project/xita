from pathlib import Path
import sys,subprocess,struct,math,json,hashlib,os
import argparse
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--reuse',action='store_true');p.add_argument('--off',action='store_true');p.add_argument('--park',action='store_true');p.add_argument('--no-markers',action='store_true');p.add_argument('--mode',type=int);p.add_argument('--case',type=int);args=p.parse_args()
S=Path(__file__).resolve().parents[1];O=args.out.resolve();O.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(S/'tools'))
from test_native_clip_region import prepare
if not args.reuse: prepare(O,args.no_markers)
import test_arm_model_palette as base
base.SIZE=2<<20
from test_arm_model_palette import Machine,RAM,PT,STACK,CTX,END,SIZE,ENV
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPSCR
class FusionMachine(Machine):
 def step(self,uc,address,size,user):
  if address==(self.symbols['observe_event']&~1):
   kind=uc.reg_read(UC_ARM_REG_R0);ctx=uc.reg_read(UC_ARM_REG_R1)
   self.events.append((kind,bytes(uc.mem_read(ctx,self.layout['size'])),hashlib.sha256(uc.mem_read(RAM,SIZE)).digest(),bytes(uc.mem_read(PT,512*4)),self.word('xv_cur_fn'),self.word('lock_depth'),uc.reg_read(UC_ARM_REG_FPSCR)))
   if args.park and kind==0x30:uc.reg_write(UC_ARM_REG_FPSCR,uc.reg_read(UC_ARM_REG_FPSCR)^0x00c00000)
  if self.imports.get(address)=='getenv':
   self.instructions+=1
   name=bytes(uc.mem_read(uc.reg_read(UC_ARM_REG_R0),64)).split(b'\0')[0]
   uc.reg_write(UC_ARM_REG_R0,0 if name==b'XV_WATCH_ADDR' else ENV);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR));return
  super().step(uc,address,size,user)
 def word(self,name):return struct.unpack('<I',self.uc.mem_read(self.symbols[name],4))[0]
 def run(self,function,fixture):
  memory,context,pages,fpscr=fixture;u=self.uc
  u.mem_write(RAM,memory);u.mem_write(CTX,context);u.mem_write(PT,struct.pack('<'+'I'*len(pages),*pages));u.mem_write(STACK,bytes(65536))
  u.reg_write(UC_ARM_REG_R0,CTX);u.reg_write(UC_ARM_REG_SP,STACK+65024);u.reg_write(UC_ARM_REG_LR,END|1);u.reg_write(UC_ARM_REG_FPSCR,fpscr)
  counters={name:self.word(name) for name in ('clip_calls','clip_register_calls')}
  self.instructions=self.copies=self.copy_bytes=self.yields=0;self.events=[]
  u.emu_start(self.symbols[function]|1,END,count=10000000);assert u.reg_read(UC_ARM_REG_PC)==END
  return dict(counters={name:(self.word(name)-value)&0xffffffff for name,value in counters.items()},context=bytes(u.mem_read(CTX,self.layout['size'])),memory=bytes(u.mem_read(RAM,SIZE)),pages=bytes(u.mem_read(PT,512*4)),fpscr=u.reg_read(UC_ARM_REG_FPSCR),events=self.events,instructions=self.instructions,copies=self.copies,clip_count=self.word('clip_count'),probe_count=self.word('probe_count'),yields=self.word('yields'))
# ARM_BUILD
cc=os.environ.get('ARM_CC',str(Path(os.environ.get('VITASDK',str(Path.home()/'vitasdk')))/'bin/arm-vita-eabi-gcc'))
common=[cc,'-O2','-g','-std=gnu11','-fno-strict-aliasing','-ffp-contract=off','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-ffunction-sections','-fdata-sections','-fstack-usage','-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_NATIVE_CLIP_REGION','-I'+str(S/'tools/tests'),'-I'+str(S/'recomp'),'-I'+str(S/'runtime')]
if args.park: common+=['-DPARK_MUTATION']
if args.no_markers: common+=['-DCLIP_TEST_NO_MARKERS']
files=[O/'reference.c',S/'recomp/kernel/xk_clip_region.c',O/'probe.c',S/'recomp/kernel/xk_clip_region_control.c',S/'tools/tests/clip_region_arm.c',S/'recomp/kernel/xk_clip.c',S/'recomp/xv_x86rt.c',S/'recomp/kernel/xk_object_jobs.c']
commands=[];elf=O/'arm-test.elf'
if '--reuse' not in sys.argv:
 objects=[];commands=[]
 for i,p in enumerate(files):
  o=O/f'arm-{i}.o';command=common+['-c',str(p),'-o',str(o)];subprocess.run(command,check=True);objects.append(str(o));commands.append(command)
 elf=O/'arm-test.elf';command=common+objects+['-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=x_str_movs,--wrap=xv_preempt,--wrap=xv_object_math_lock,--wrap=xv_object_math_unlock,--undefined=run_original,--undefined=run_current,--undefined=run_fused,--undefined=layout','-lm','-lgcc','-o',str(elf)];subprocess.run(command,check=True);commands.append(command)
m=FusionMachine(elf);layout=m.layout;m.uc.mem_write(m.symbols['initialized'],struct.pack('<I',1))
empty=(bytes(SIZE),bytes(layout['size']),[i*4096 for i in range(SIZE//4096)],0);m.run('test_boot',empty);rows=[];failures=[]
if '--off' in sys.argv:m.uc.mem_write(m.symbols['state'],bytes(4))
for mode in ([int(sys.argv[sys.argv.index('--mode')+1])] if '--mode' in sys.argv else range(16)):
 for case in ([int(sys.argv[sys.argv.index('--case')+1])] if '--case' in sys.argv else range(64)):
  memory=bytearray(b'\xa5'*SIZE);context=bytearray(layout['size']);pages=[(i^1)*4096 for i in range(SIZE//4096)]
  def put(a,data):
   for j,v in enumerate(data):memory[pages[(a+j)>>12]+((a+j)&4095)]=v
  def word(a,v):put(a,struct.pack('<I',v))
  def fp(a,v):put(a,struct.pack('<f',v))
  def field(n,v,fmt='I'):struct.pack_into('<'+fmt,context,layout[n],v)
  def reg(i,v):struct.pack_into('<I',context,layout['r']+i*4,v)
  n=3+case%30;edges=4+case%4;cap=n+16;poly=0x18ff0+case%4;clip=0x38ff0+case%4;out=0x58ff0+case%4;sp=0x91ff0+(case%4)*4
  if case%23==0:n=0
  if case%29==0:n=1
  if case%31==0:n=2
  if case%47==0:n=128
  if case%13==0:edges=0
  if case%17==0:edges=1
  if case%19==0:edges=2
  if case%43==0:edges=0xffff
  if case%7==0:cap=0
  elif case%11==0:cap=n-1
  if case%9==0:out=poly
  elif case%9==1:out=poly+4
  elif case%9==2:out=poly-4
  elif case%9==3:pages[0x58]=pages[0x18];pages[0x59]=pages[0x19]
  if case%53==0:out=clip
  if case%59==0:poly=sp-0x2020+0x20
  if case%61==0:out=sp-0x2020+0x14
  for j in range(n):
   a=math.tau*j/n;r=[.25,1.25,2.5][case%3];fp(poly+j*8,math.cos(a)*r);fp(poly+j*8+4,math.sin(a)*r)
  if edges!=0xffff:
   for j in range(edges):
    a=(1 if case&1 else -1)*math.tau*j/edges;fp(clip+j*8,math.cos(a));fp(clip+j*8+4,math.sin(a))
  if edges>1 and edges!=0xffff and case%5==0:
   for j in range(8):put(clip+8+j,bytes([memory[pages[(clip+j)>>12]+((clip+j)&4095)]]))
  if case>=48 and n:
   exceptional=[0,0x80000000,1,0x807fffff,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0x7f801234]
   word(poly+4,exceptional[(case-48)%len(exceptional)])
  fp(0x1F0A68,0);fp(0x1F0A78,1);put(0x1F0AF8,struct.pack('<d',9.999999747378752e-05))
  for j,v in enumerate((0x12345678,edges,clip,cap,out)):word(sp+j*4,v)
  fp(sp+20,[0,.0001,-.0001][case%3])
  for j in range(8):reg(j,0x87654321+j);struct.pack_into('<d',context,layout['st']+j*8,j+.375)
  reg(4,sp);reg(1,n);reg(2,poly);field('fsp',case%8);field('fsw',0xabcd,'H');field('fcw',0x37f,'H');field('f_kind',2);field('f_bits',32);field('preempt',1 if case%3==0 else 100000)
  fixture=(bytes(memory),bytes(context),pages,mode<<22)
  result=[m.run(fn,fixture) for fn in ('run_original','run_current','run_fused')]
  keys=('context','memory','pages','fpscr','events','clip_count','probe_count','yields')
  comparisons={name:[k for k in keys if result[a][k]!=result[b][k]] for name,a,b in [('original_current',0,1),('original_fused',0,2),('current_fused',1,2)]}
  if any(comparisons.values()):
   failures.append({'mode':mode,'case':case,'fields':comparisons})
   print('STRICT MISMATCH',failures[-1],flush=True)
   for k in set().union(*comparisons.values()):
    for i,x in enumerate(result):
     if isinstance(x[k],bytes):(O/f'fail-{mode}-{case}-{i}-{k}.bin').write_bytes(x[k])
  assert result[1]['counters']['clip_calls']==result[1]['counters']['clip_register_calls']==result[0]['clip_count']
  assert result[2]['counters']['clip_calls']==result[2]['counters']['clip_register_calls']==result[0]['clip_count']
  rows.append({'strict_fields':comparisons,'mode':mode,'case':case,'n':n,'edges':edges,'capacity':cap,'clips':result[0]['clip_count'],'yields':result[0]['yields'],'result':struct.unpack_from('<h',result[0]['context'],layout['r'])[0],'original':result[0]['instructions'],'current':result[1]['instructions'],'fused':result[2]['instructions']})
  if case%16==0:print('PASS',mode,case,rows[-1],flush=True)
report={'variant':'production clip-region helper/control/common counters','strict_failures':failures,'rows':rows,'fixtures':len(rows),'commands':commands,'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'limits':['Owner-idle actual math-lock path; real pthread worker acceptance is separate.','Observer snapshots/hash executed by model; their C call overhead remains.','Memory-copy imports modeled; instruction counts not cycle/FPS claim.','With --park the compiled park callback mutates context/scratch and the observer changes FPSCR; host fixtures also relocate/remap memory.']}
(O/('arm-off-result.json' if '--off' in sys.argv else 'arm-result.json')).write_text(json.dumps(report,indent=2)+'\n');print('COMPLETE',len(rows),'strict failures',len(failures),{k:sum(r[k] for r in rows) for k in ('original','current','fused')})

assert not failures, 'strict ARM mismatches'
