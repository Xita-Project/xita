"""Execute the owned deferred setter/helper/copier/commit on synthetic buffers.

Requires Unicorn. No sound API is replaced and no owned bytes are embedded.
The oracle validates source ABI, retained shadow gap, coalescing and command
publication; real GP monitor consumption is a separate test.
"""
from pathlib import Path
import sys,struct,json,argparse,hashlib
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
parser=argparse.ArgumentParser(description='Verify owned XDK reverb deferred writes and commit using synthetic buffers; no DSP emulation implied.')
parser.add_argument('xbe',type=Path);parser.add_argument('--out',type=Path,required=True);args=parser.parse_args()
out=args.out.resolve()
if out.is_relative_to(ROOT):parser.error('oracle output must remain outside checkout')
out.mkdir(parents=True,exist_ok=False)
from recompiler.xita_recomp import Image
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.x86_const import *
im=Image(str(args.xbe))
from recompiler.core.profile import load_profile
load_profile('halo2_5849').validate_image(im)
u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0,0x800000)
for va,_,n,*_ in im.secs:u.mem_write(va,im.bytes_at(va,n))
def w(a,*x):u.mem_write(a,struct.pack('<'+'I'*len(x),*x))
def r(a,n=1):return struct.unpack('<'+'I'*n,u.mem_read(a,n*4))
dsp=next(s[0]for s in im.secs if s[4]=='DSPImage')
code=im.u32(dsp+0x804);table=im.u32(dsp+0x808)+4*im.u32(dsp+0x80c)
state=im.u32(dsp+table+8+9*32+8)
manager=0x700100;scratch=0x600000;start=state+16
w(manager+8,0x700200);w(0x700210,0x700300);w(0x700300,scratch);w(manager+0x20,0x700400);w(0x700400,15)
w(0x700400+8+9*32+8,(state-code*4-0x17d0618)&0xffffffff)
w(manager+0x18,0x8000,0);w(scratch+0x804,code);w(scratch+0x810,0)
u.mem_write(scratch+state,bytes([0xa5]*544));w(0x701000,7);data=list(range(0x123400,0x123442));w(0x701010,*data)
seen=set();writes=[]
u.hook_add(UC_HOOK_CODE,lambda uc,a,n,user:seen.add(a))
def hook(uc,access,a,n,v,user):
 assert (scratch+state<=a and a+n<=scratch+state+544) or (scratch+0x800<=a and a+n<=scratch+0x814) or (manager+0x18<=a and a+n<=manager+0x20) or (0x70ef00<=a and a+n<=0x70f018),(hex(a),n)
 writes.append((a,n))
u.hook_add(UC_HOOK_MEM_WRITE,hook)
def call(entry,args):
 w(0x70f000,0x780000,*args);u.reg_write(UC_X86_REG_ESP,0x70f000);u.reg_write(UC_X86_REG_ECX,manager);u.reg_write(UC_X86_REG_EFLAGS,0x202)
 for reg,v in [(UC_X86_REG_EBX,0x12345678),(UC_X86_REG_ESI,0x23456789),(UC_X86_REG_EDI,0x3456789a),(UC_X86_REG_EBP,0x456789ab)]:u.reg_write(reg,v)
 u.emu_start(entry,0x780000,count=10000);assert u.reg_read(UC_X86_REG_EIP)==0x780000;assert u.reg_read(UC_X86_REG_ESP)==0x70f004+4*len(args)
 for reg,v in [(UC_X86_REG_EBX,0x12345678),(UC_X86_REG_ESI,0x23456789),(UC_X86_REG_EDI,0x3456789a),(UC_X86_REG_EBP,0x456789ab)]:assert u.reg_read(reg)==v
call(0x37e52f,[9,16,0x701000,4,1]);assert r(manager+0x18,2)==(start,4)
call(0x37e52f,[9,280,0x701010,264,1]);assert r(manager+0x18,2)==(start,528)
assert r(scratch+start)==(7,);assert bytes(u.mem_read(scratch+state+20,260))==bytes([0xa5]*260);assert r(scratch+state+280,66)==tuple(data)
call(0x37e3c5,[0,0]);assert r(manager+0x18,2)==(0x8000,0)
expected=((start>>2)-code-0x206,code,start,132,2);assert r(scratch+0x800,5)==expected
assert r(0x701000)==(7,);assert r(0x701010,66)==tuple(data)
report={'passed':True,'distinct_original_instructions':len(seen),'dirty_extent':[start,528],'monitor_header':expected,'input_unchanged':True,'saved_shadow_gap_retained':True,'nonvolatile_and_esp_preserved':True,'no_substituted_functions':True,'xbe_sha256':hashlib.sha256(args.xbe.read_bytes()).hexdigest()}
(out/'deferred-oracle-result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
