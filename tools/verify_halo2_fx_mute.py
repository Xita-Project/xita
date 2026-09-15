"""Owned-XBE active FX mute oracle, with synthetic objects and mapped FIFO.

Only allocation and command capacity are isolated. No sound setter is
substituted. Reports can contain owned descriptors and stay outside Git.
"""
from pathlib import Path
import sys,struct,json,hashlib
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
import argparse
parser=argparse.ArgumentParser(description="Audit original Halo 2 FX15..22 construction, routing, Play and active mute; no device/output emulation.")
parser.add_argument('xbe',type=Path)
parser.add_argument('--bin',type=int,choices=range(15,23),required=True,dest='input_bin')
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args();out=args.out.resolve()
if out.is_relative_to(ROOT):parser.error('owned oracle outputs must remain outside the checkout')
out.mkdir(parents=True,exist_ok=False)
from recompiler.xita_recomp import Image
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ
from unicorn.x86_const import *
im=Image(str(args.xbe));u=Uc(UC_ARCH_X86,UC_MODE_32)
u.mem_map(0,0x700000);u.mem_map(0x900000,0x100000);u.mem_map(0xfe820000,0x1000)
for va,off,size,virtual,name,flags in im.secs:
 if size:u.mem_write(va,im.data[off:off+size])
from recompiler.core.profile import load_profile
load_profile('halo2_5849').validate_image(im)
input_bin=args.input_bin
route_bin=6+(input_bin-15)%4
PARAMS=0x900000;DESC=0x901000;VOICE=0x904000;DEVICE=0x905000;SP=0x9e0800;END=0x9f0000
u.mem_write(0x24,b'\x02');u.mem_write(0x58,bytes(4));u.mem_write(0x386B0C,bytes(4)); u.mem_write(0x3872c0,struct.pack('<I',0x930000)); u.mem_write(0x387330,struct.pack('<I',0x940000))
def put(a,*v):u.mem_write(a,struct.pack('<'+'I'*len(v),*[x&0xffffffff for x in v]))
def word(a):return struct.unpack('<I',u.mem_read(a,4))[0]
heap=0x910000;ips=set();allocs=[];mmio=[];reads=[];filter_views=[]
def code(uc,a,n,data):
 global heap
 ips.add(a)
 if a==0x38137a:
  bp=uc.reg_read(UC_X86_REG_EBP);left,right=word(bp-0x14),word(bp-0x10)
  filter_views.append({'left':f'{left:08X}','right':f'{right:08X}','left_sha256':hashlib.sha256(bytes(uc.mem_read(left,32))).hexdigest(),'right_sha256':hashlib.sha256(bytes(uc.mem_read(right,32))).hexdigest()})
 if a==0x37D952:
  sp=uc.reg_read(UC_X86_REG_ESP);tag,size,zero=struct.unpack('<3I',uc.mem_read(sp+4,12));assert size in (0xa4,0x38) and zero==1
  allocs.append((heap,size));uc.mem_write(heap,bytes(size));uc.reg_write(UC_X86_REG_EAX,heap);heap+=0x1000
  uc.reg_write(UC_X86_REG_ESP,sp+16);uc.reg_write(UC_X86_REG_EIP,word(sp))
def write(uc,access,a,n,v,data):
 if 0xfe820000<=a<a+n<=0xfe821000:mmio.append((f'{uc.reg_read(UC_X86_REG_EIP):08X}',f'{a:08X}',n,f'{v:08X}'));return
 assert (0x900000<=a and a+n<=0xa00000) or (0x3871c8<=a and a+n<=0x387208),(hex(a),n,hex(uc.reg_read(UC_X86_REG_EIP)))
def read(uc,access,a,n,v,data):
 if a>=0xfe000000:
  assert a==0xfe820010 and n==4,(hex(a),n)
  reads.append(a);put(a,0x1000) # audit-only command FIFO capacity; no device/output emulation
u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_WRITE,write);u.hook_add(UC_HOOK_MEM_READ,read)
def call(ip,args=[],this=0):
 put(SP,END,*args);u.reg_write(UC_X86_REG_ESP,SP);u.reg_write(UC_X86_REG_ECX,this)
 u.reg_write(UC_X86_REG_FPCW,0x37f);u.reg_write(UC_X86_REG_FPSW,0);u.reg_write(UC_X86_REG_FPTAG,0xffff)
 for r in (UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI):u.reg_write(r,0x12345678)
 u.emu_start(ip,END,count=200000)
 assert u.reg_read(UC_X86_REG_EIP)==END,(hex(u.reg_read(UC_X86_REG_EIP)),hex(ip))
 assert u.reg_read(UC_X86_REG_ESP)==SP+4*(len(args)+1)
 for r in (UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI):assert u.reg_read(r)==0x12345678
 return u.reg_read(UC_X86_REG_EAX)
call(0x37E126)
put(PARAMS,0x41718c,1);put(DESC,24,0x100000,0,0,0,input_bin)
assert call(0x37CEF1,[DESC],PARAMS)==0
put(DEVICE,0x417254,1);put(DEVICE+0x58,0x906000,0x906004);put(0x906000,192,64)
assert call(0x382B22,[DEVICE,PARAMS],VOICE)==VOICE
assert call(0x382F58,[],VOICE)==0
setup_mmio=mmio.copy();mmio.clear()
L=0x908030;OBJECT=0x909000
put(DEVICE+0xc,L-0x30)
u.mem_write(L+8,im.bytes_at(0x3857ec,60));put(L,0x25)
put(L+0x20,0x3f800000,0,0);put(L+0x2c,0,0x3f800000,0)
put(L+0x38,0x4043126f,0,0);put(L+0x50,0xfff);u.mem_write(L+0x54,im.bytes_at(0x3857b8,48))
for off in (0x488,0x490,0x498):put(DEVICE+off,DEVICE+off,DEVICE+off)
put(OBJECT+0x10,PARAMS);put(OBJECT+0x20,VOICE);put(OBJECT+0x0c,VOICE)
before=bytes(u.mem_read(PARAMS,0xe0));mmio.clear()
assert call(0x37B66F,[OBJECT+0x1c,0])==0
assert bytes(u.mem_read(PARAMS,0xe0))==before
zero_writes=mmio.copy();mmio.clear()
LIST=0x90a000;PAIRS=0x90a010
put(LIST,1,PAIRS);put(PAIRS,route_bin,0)
assert call(0x37C5E4,[OBJECT+0x1c,LIST])==0
assert word(PARAMS+0x24)==1 and bytes(u.mem_read(PARAMS+0x28,1))==bytes([route_bin])
route_writes=mmio.copy();mmio.clear()
assert call(0x37B6DF,[OBJECT+0x1c,0,0,0])==0
assert word(PARAMS+0xd4)==input_bin
play_writes=mmio.copy();mmio.clear();before_params=bytes(u.mem_read(PARAMS,0xe0));before_voice=bytes(u.mem_read(VOICE,0x158));before_volume_ips=set(ips)
assert call(0x37B66F,[OBJECT+0x1c,(-6400)&0xffffffff])==0
assert word(PARAMS+0x1c)==((-6400)&0xffffffff)
assert word(PARAMS+0x24)==1 and bytes(u.mem_read(PARAMS+0x28,1))==bytes([route_bin])
mute_writes=mmio.copy();volume_addresses=[row for row in mute_writes if row[1] in ['FE820360','FE820364','FE820368']]
assert len(volume_addresses)==3 and all(row[3]=='FFFFFFFF' for row in volume_addresses),volume_addresses
after_params=bytes(u.mem_read(PARAMS,0xe0));after_voice=bytes(u.mem_read(VOICE,0x158))
assert all(a==b or 0x1c<=i<0x20 for i,(a,b)in enumerate(zip(before_params,after_params)))
assert after_voice==before_voice

report={'input_bin':input_bin,'route_bin':route_bin,'xbe_sha256':hashlib.sha256(im.data).hexdigest(),'allocations':allocs,'params':bytes(u.mem_read(PARAMS,0xe0)).hex(),'voice':bytes(u.mem_read(VOICE,0x158)).hex(),'setup_mmio':setup_mmio,'zero_volume_mmio':zero_writes,'route_mmio':route_writes,'play_mmio':play_writes,'mute_mmio':mute_writes,'params_only_volume_changed':True,'mute_preserved_route':True,'mmio_reads':len(reads),'unique_instruction_addresses':len(ips),'scope':'Original nonspatial FX15..22 constructor/setup, zero-volume, one-bin route, Play then active SetVolume(-6400). Allocator/FIFO capacity isolated; this is not hardware/output execution.'}
(out/'original-mute.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({"passed":True,"bin":input_bin,"route":route_bin,"original_instruction_addresses":len(ips),"params_only_volume_changed":True,"voice_unchanged":after_voice==before_voice,"packed_attenuation_words":[row[3]for row in volume_addresses]}))
