#!/usr/bin/env python3
"""Run the Vita-compiled exact comparator against its native libc in Cortex-A9 emulation.
Requires unicorn and pyelftools; instruction counts are not hardware timings.
"""
from pathlib import Path
import argparse,hashlib,json,random,subprocess,time
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE,UC_HOOK_CODE,UC_PROT_ALL
from unicorn.arm_const import *
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--blocks',action='store_true',help='Also check the experimental multi-vector loads against the original comparator')
parser.add_argument('--wide',action='store_true',help='Enable the optional 256-byte equality reduction')
parser.add_argument('--baseline-elf',required=True,type=Path)
parser.add_argument('--output-dir',required=True,type=Path)
args=parser.parse_args()
if args.wide and not args.blocks:parser.error('--wide requires --blocks to exercise the wide comparator')
out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
wrapper=out/'wrapper.c';binary=out/'bytes-equal-arm.elf'
wrapper.write_text('#include "runtime/xv_bytes_equal.h"\nint test_equal(const void *a,const void *b,unsigned n,unsigned blocks) { '+
                  ('return blocks ? xv_bytes_equal_blocks(a,b,n) : xv_bytes_equal(a,b,n);' if args.blocks else 'return xv_bytes_equal(a,b,n);')+' }\n')
subprocess.run(['arm-vita-eabi-gcc','-O2','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-ffreestanding','-fno-builtin','-nostdlib','-I',str(root),f'-DXV_VERTEX_WIDE_COMPARE={int(args.wide)}',str(wrapper),'-Wl,-Ttext=0x10000,-e,test_equal','-o',str(binary)],check=True)
uc=Uc(UC_ARCH_ARM,UC_MODE_ARM);uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
uc.reg_write(UC_ARM_REG_C1_C0_2,15<<20);uc.reg_write(UC_ARM_REG_FPEXC,1<<30)
functions={}
def load(path,symbol):
 with open(path,'rb') as f:
  elf=ELFFile(f);sym=elf.get_section_by_name('.symtab').get_symbol_by_name(symbol)[0];base=sym['st_value']&~1;size=sym['st_size'];sec=elf.get_section(sym['st_shndx']);data=sec.data()[base-sec['sh_addr']:base-sec['sh_addr']+size]
  start=base&~4095;uc.mem_map(start,((base+size+4095)&~4095)-start);uc.mem_write(base,data);functions[symbol]=(base|1,size)
load(str(binary),'test_equal')
if args.blocks:functions['block_equal']=functions['test_equal']
load(str(args.baseline_elf),'memcmp')
A=0x200000;B=0x400000;S=0x700000;END=0x900000;CAP=0x100000
for addr,size in [(A,CAP),(B,CAP),(S,65536),(END,4096)]:uc.mem_map(addr,size)
allowed=[];faults=[];instructions=0;native_calls=0

def access(uc,kind,address,size,value,user):
 if S<=address and address+size<=S+65536:return
 if kind==16 and any(lo<=address and address+size<=hi for lo,hi in allowed):return
 faults.append((kind,hex(address),size,allowed.copy()));uc.emu_stop()
uc.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,access)
def count(uc,address,size,user):
 global instructions
 instructions+=1

def invoke(name,a,b,n):
 global allowed,native_calls
 native_calls+=1
 allowed=[(a,a+n),(b,b+n)]
 for reg,value in [(UC_ARM_REG_R0,a),(UC_ARM_REG_R1,b),(UC_ARM_REG_R2,n),(UC_ARM_REG_R3,int(name=='block_equal')),(UC_ARM_REG_SP,S+65024),(UC_ARM_REG_LR,END|1)]:uc.reg_write(reg,value)
 uc.emu_start(functions[name][0],END,count=3000000)
 assert not faults,faults
 assert uc.reg_read(UC_ARM_REG_PC)==END,hex(uc.reg_read(UC_ARM_REG_PC))
 value=uc.reg_read(UC_ARM_REG_R0)
 return not value if name=='memcmp' else bool(value)
rng=random.Random(0x19072026);cases=0;started=time.monotonic()
def case(n,oa,ob,position):
 global cases
 data=rng.randbytes(n);changed=bytearray(data)
 if position is not None:changed[position]^=1<<rng.randrange(8)
 a=A+4096+oa;b=B+4096+ob;uc.mem_write(a,data);uc.mem_write(b,bytes(changed))
 expected=position is None
 for name in functions:assert invoke(name,a,b,n)==expected,(name,n,oa,ob,position)
 cases+=1
# All alignment pairs around each vector boundary. Random mismatch positions
# plus first/last bytes exercise every vector lane and the scalar tail.
for n in [0,1,2,3,4,7,15,16,17,31,32,33,47,48,49,63,64,65,79,80,81,127,128,129,255,256,257]:
 for oa in range(16):
  for ob in range(16):
   case(n,oa,ob,None)
   if n:case(n,oa,ob,rng.randrange(n))
for n in range(1,130):
 for position in range(n):case(n,n%16,(n*7)%16,position)
if args.wide:
 for n in [319,320,321,575,576,577]:
  for oa in range(16):
   for ob in range(16):
    case(n,oa,ob,None)
    case(n,oa,ob,rng.randrange(n))
 for n in [320,321,512,513,576,1024]:
  for position in range(n):case(n,n%16,(n*7)%16,position)
for n in [511,512,513,1023,1024,1025,4095,4096,4097,65535,65536]:
 for pos in [None,0,n//2,n-1]:case(n,rng.randrange(16),rng.randrange(16),pos)
for name in ('test_equal','block_equal') if args.blocks else ('test_equal',):
 assert invoke(name,0,0,0)
 assert invoke(name,A,A,4096)
# Pages immediately after input are unmapped. Neither routine may overread.
for n in [1,15,16,17,63,64,65,255,256,257,319,320,321,575,576,577,4096]:
 a=A+CAP-n;b=B+CAP-n;data=rng.randbytes(n);uc.mem_write(a,data);uc.mem_write(b,data)
 for name in functions:assert invoke(name,a,b,n)
# Dynamic instruction counts only: these are not CPU cycles or hardware time.
rows=[];hook=uc.hook_add(UC_HOOK_CODE,count)
for start,size in set(functions.values()):uc.ctl_remove_cache(start&~1,(start&~1)+size)
for n in [16,64,256,4096,65536]:
 data=rng.randbytes(n);uc.mem_write(A,data);uc.mem_write(B,data);row={'bytes':n}
 for name in functions:
  instructions=0;assert invoke(name,A,B,n);assert instructions>0;row[name+'_instructions']=instructions
 rows.append(row)
uc.hook_del(hook)
report={'wide':args.wide,'cases':cases,'native_calls':native_calls,'bounds_faults':faults,'seconds':time.monotonic()-started,'instruction_counts_not_cycles':rows,'functions':functions,'header_sha256':hashlib.sha256((root/'runtime/xv_bytes_equal.h').read_bytes()).hexdigest(),'baseline_elf_sha256':hashlib.sha256(args.baseline_elf.read_bytes()).hexdigest()}
(out/'results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
