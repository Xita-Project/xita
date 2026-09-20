#!/usr/bin/env python3
"""Execute Vita-compiled marker snapshot fixtures; not hardware FPS validation."""
from pathlib import Path
import argparse,struct,json,random,subprocess
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reference',type=Path,required=True,help='Private reference.c from test_marker_snapshot.py')
parser.add_argument('--output-dir',type=Path,required=True)
parser.add_argument('--cc',default='arm-vita-eabi-gcc')
args=parser.parse_args();args.output_dir.mkdir(parents=True,exist_ok=True)
p=args.output_dir
command=[args.cc,'-O3','-funroll-loops','-fno-strict-aliasing','-ffp-contract=off',
 '-mthumb','-mcpu=cortex-a9','-mfpu=neon','-std=gnu11','-I'+str(ROOT/'recomp'),
 '-ffunction-sections','-fdata-sections',str(args.reference),
 str(ROOT/'tools/tests/marker_snapshot_arm.c'),str(ROOT/'tools/tests/marker_snapshot_arm_stubs.c'),
 str(ROOT/'recomp/kernel/xk_math.c'),str(ROOT/'recomp/xv_x86rt.c'),'-nostdlib',
 '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--undefined=original_marker,--undefined=current_marker,--undefined=snapshot_marker,--undefined=layout',
 '-lgcc','-o',str(p/'arm.elf')]
subprocess.run(command,check=True)
import test_arm_model_palette as arm
arm.SIZE=2<<20
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_FPSCR,UC_ARM_REG_PC
class Machine(arm.Machine):
 def run(self,fn,memory,context,fpscr):
  u=self.uc;u.mem_write(arm.RAM,memory);u.mem_write(arm.CTX,context)
  u.mem_write(arm.PT,struct.pack('<'+'I'*(arm.SIZE//4096),*range(0,arm.SIZE,4096)))
  u.mem_write(arm.STACK,bytes(65536));u.reg_write(UC_ARM_REG_R0,arm.CTX)
  u.reg_write(UC_ARM_REG_SP,arm.STACK+65024);u.reg_write(UC_ARM_REG_LR,arm.END|1);u.reg_write(UC_ARM_REG_FPSCR,fpscr)
  self.instructions=self.copies=self.copy_bytes=self.yields=0
  u.emu_start(self.symbols[fn]|1,arm.END,count=200000)
  assert u.reg_read(UC_ARM_REG_PC)==arm.END
  return bytes(u.mem_read(arm.CTX,self.layout['size'])),bytes(u.mem_read(arm.RAM,arm.SIZE)),u.reg_read(UC_ARM_REG_FPSCR)&0xfffffff
m=Machine(p/'arm.elf');rnd=random.Random(7319)
edge=[0,0x80000000,1,0x00800000,0x7f800000,0xff800000,0x7fc01234,0x7f801234]
for k in range(256):
 mem=bytearray(b'\xa5'*arm.SIZE);c=bytearray(m.layout['size'])
 def put(a,v):struct.pack_into('<I',mem,a,v&0xffffffff)
 regs=[rnd.getrandbits(32) for i in range(8)];node=k%17-8;regs[0]=(regs[0]&0xffff0000)|(node&0xffff);regs[4]=0x41000;regs[6]=0x21000;regs[7]=0x11000
 struct.pack_into('<8I',c,m.layout['r'],*regs)
 struct.pack_into('<8d',c,m.layout['st'],*[i+.25 for i in range(8)])
 struct.pack_into('<I',c,m.layout['fsp'],k%8);struct.pack_into('<H',c,m.layout['fsw'],k*131&65535);struct.pack_into('<H',c,m.layout['fcw'],0x37f)
 put(0x41024,0x32000);put(0x1f0a68,0);put(0x1f0b04,0x40000000);put(0x1f0a78,0x3f800000)
 for j in range(20):
  word=edge[(k+j)%8] if k%3==0 else (rnd.getrandbits(32)&0x807fffff)|((115+k%20)<<23)
  a=0x11010+j*4 if j<4 else 0x11004+(j-4)*4 if j<7 else 0x32000+node*52+(j-7)*4
  put(a,word)
 fpscr=((k%4)<<22)|(((k//4)%4)<<24)|(0x9f if k%2 else 0)
 before=m.run('current_marker',bytes(mem),bytes(c),fpscr);after=m.run('snapshot_marker',bytes(mem),bytes(c),fpscr)
 if before!=after:
  for i,n in enumerate(('ctx','mem','fpscr')):
   if before[i]!=after[i]:print('mismatch',k,n, next(((j,x,y) for j,(x,y) in enumerate(zip(before[i],after[i])) if x!=y),None) if i<2 else (before[i],after[i]))
  raise SystemExit(1)
print('PASS 256 Vita-linked marker current/snapshot full context/memory/FPSCR cases')
(p/'arm-result.json').write_text(json.dumps({'command':command,'cases':256,'scope':'exact current native vs snapshot; no ownership or FPS proof'})+'\n')
