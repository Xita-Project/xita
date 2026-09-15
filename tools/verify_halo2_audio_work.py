"""Run the owned low-priority DirectSound work body on synthetic task queues.

Only interrupt guards and synthetic callbacks are isolated. This proves queue
ordering/ABI, not APU work, real audio completion timing or menu compatibility.
Requires Unicorn and iced_x86; report output must remain outside the checkout.
"""
from pathlib import Path
import argparse,sys,struct,json,hashlib
src=Path(__file__).resolve().parents[1];sys.path.insert(0,str(src))
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('xbe',type=Path)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args();p=args.out.resolve()
if p.is_relative_to(src):parser.error('oracle output must remain outside checkout')
p.mkdir(parents=True,exist_ok=False)
from recompiler.xita_recomp import Image
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import *
im=Image(str(args.xbe))
from recompiler.core.profile import load_profile
load_profile('halo2_5849').validate_image(im)
u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x370000,0x20000);u.mem_map(0x500000,0x10000)
s=im.section_of(0x37ebeb);u.mem_write(s[0],im.bytes_at(s[0],s[2]))
def w(a,*v):u.mem_write(a,struct.pack('<'+'I'*len(v),*v))
def r(a):return struct.unpack('<I',u.mem_read(a,4))[0]
head=0x5004b0;tasks=[0x502000+i*0x20 for i in range(8)];objects=[0x503000+i*0x20 for i in range(8)];vt=0x504000;cb=0x505000;finish=0x505100
seen=[];irq=[];executed=set();append_on_callback=False

def link(head,items):
 seq=[head]+items+[head]
 for index,a in enumerate(seq[1:-1],1):w(a,seq[index+1],seq[index-1])
 w(head,seq[1],seq[-2])

def order():
 out=[];a=r(head)
 while a!=head:
  assert a in tasks and a not in out;out.append(a);a=r(a)
 return [tasks.index(x)for x in out]

def hook(uc,address,size,user):
 global append_on_callback
 executed.add(address)
 if address in (0x379c90,0x379cb2):
  irq.append(address);sp=u.reg_read(UC_X86_REG_ESP);u.reg_write(UC_X86_REG_EIP,r(sp));u.reg_write(UC_X86_REG_ESP,sp+4)
 if address==cb:
  sp=u.reg_read(UC_X86_REG_ESP);obj=u.reg_read(UC_X86_REG_ECX);idx=objects.index(obj)
  seen.append((idx,r(sp+4),r(sp+8)))
  if append_on_callback:
   append_on_callback=False;a=tasks[7];tail=r(head+4);w(a,head,tail);w(tail,a);w(head+4,a)
  u.reg_write(UC_X86_REG_EIP,r(sp));u.reg_write(UC_X86_REG_ESP,sp+12)
 if address==0x37e9ab:raise AssertionError('unexpected DSP reset')
u.hook_add(UC_HOOK_CODE,hook)
reports=[]
for initial in ([],[0],[0,1,2],[2,0,1]):
 for repeat in (0,1,2,7):
  for append in (False,True):
   u.mem_write(0x500000,bytes(0x10000));w(0x387108,0x504100);w(0x504100,0xcccccc);w(vt+0x24,cb)
   for i,a in enumerate(tasks):w(a,a,a,2|(4 if repeat&(1<<i)else 0),objects[i],0x10+i,0x20+i);w(objects[i],vt)
   link(head,[tasks[i]for i in initial]);seen.clear();irq.clear();append_on_callback=append
   sp=0x50f000;w(sp,finish);regs={UC_X86_REG_EAX:0x11223344,UC_X86_REG_EBX:0x22334455,UC_X86_REG_ECX:0x500000,UC_X86_REG_EDX:0x33445566,UC_X86_REG_ESI:0x44556677,UC_X86_REG_EDI:0x55667788,UC_X86_REG_EBP:0x66778899,UC_X86_REG_ESP:sp,UC_X86_REG_EFLAGS:0x202}
   for reg,value in regs.items():u.reg_write(reg,value)
   u.emu_start(0x37ebeb,finish,count=20000);assert u.reg_read(UC_X86_REG_EIP)==finish
   assert seen==[(i,0x10+i,0x20+i)for i in initial]
   expected=[]
   for j,i in enumerate(initial):
    if repeat&(1<<i):expected.append(i)
    if j==0 and append:expected.append(7)
   assert order()==expected,(initial,repeat,append,order(),expected)
   for i in initial:assert r(tasks[i]+8)==(6 if repeat&(1<<i)else 0)
   for reg in (UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP):assert u.reg_read(reg)==regs[reg]
   assert u.reg_read(UC_X86_REG_ESP)==sp+4
   assert irq.count(0x379c90)==irq.count(0x379cb2)==len(initial)+1
   reports.append({'initial':initial,'repeat_mask':repeat,'append_during_callback':append,'called':[x[0]for x in seen],'remaining':order()})
report={'passed':True,'cases':len(reports),'original_instruction_addresses':len(executed-{cb,0x379c90,0x379cb2}),'work_body_sha256':hashlib.sha256(im.bytes_at(0x37ebeb,168)).hexdigest(),'scope':'original low-priority work body and list helpers; only interrupt guard and synthetic callback isolated, valid DSP canary; FIFO snapshot, once/repeated flags, new work retained for next call, callback ABI/nonvolatile state','fixtures':reports}
(p/'work-oracle-result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items()if k!='fixtures'}))
