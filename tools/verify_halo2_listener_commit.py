"""Original listener commit oracle using owned executable and synthetic objects.

The exact active spatial children are audited separately. No function is
substituted, no owned bytes are embedded, and all outputs remain private.
"""
from pathlib import Path
import sys,struct,json,hashlib
import argparse
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
parser=argparse.ArgumentParser(description='Execute the original Halo 2 listener constructor, scalar/deferred setters and fixed device commit; no hardware or child processing is emulated.')
parser.add_argument('xbe',type=Path);parser.add_argument('--out',type=Path,required=True);args=parser.parse_args();p=args.out.resolve()
if p.is_relative_to(ROOT):parser.error('owned oracle output must remain outside checkout')
p.mkdir(parents=True,exist_ok=False)
from recompiler.xita_recomp import Image
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE,UC_HOOK_MEM_READ
from unicorn.x86_const import *
im=Image(str(args.xbe))
from recompiler.core.profile import load_profile
load_profile('halo2_5849').validate_image(im)
u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0,0xa00000)
for va,off,size,*_ in im.secs:
 if size:u.mem_write(va,im.data[off:off+size])
P=0x900000;D=0x901000;LOW=0x902000;SP=0x9e0800;END=0x9f0000
w=lambda a,*v:u.mem_write(a,struct.pack('<'+'I'*len(v),*[x&0xffffffff for x in v]))
r=lambda a:struct.unpack('<I',u.mem_read(a,4))[0]
u.mem_write(0x24,b'\x02');w(0x58,0);w(0x386b0c,0);w(0x386b08,0)
w(D+8,P,LOW,D+0x10,D+0x10);w(LOW+0xc,P)
ips=set();writes=[]
u.hook_add(UC_HOOK_CODE,lambda uc,a,n,z:ips.add(a))
def write(uc,access,a,n,v,z):
 assert 0x900000<=a and a+n<=0xa00000 or a==0x386b04 and n==4,(hex(a),n,hex(uc.reg_read(UC_X86_REG_EIP)))
 writes.append((a,n))
u.hook_add(UC_HOOK_MEM_WRITE,write)
def read(uc,access,a,n,v,z):assert a<0xfe000000,(hex(a),n)
u.hook_add(UC_HOOK_MEM_READ,read)
def call(ip,args=[],this=0):
 w(SP,END,*args);u.reg_write(UC_X86_REG_ESP,SP);u.reg_write(UC_X86_REG_ECX,this);u.reg_write(UC_X86_REG_EFLAGS,0x202)
 u.reg_write(UC_X86_REG_FPCW,0x23f);u.reg_write(UC_X86_REG_FPSW,0);u.reg_write(UC_X86_REG_FPTAG,0xffff)
 for reg in (UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI):u.reg_write(reg,0x12345678)
 u.emu_start(ip,END,count=20000);assert u.reg_read(UC_X86_REG_EIP)==END and u.reg_read(UC_X86_REG_ESP)==SP+4*(1+len(args))
 for reg in (UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI):assert u.reg_read(reg)==0x12345678
 return u.reg_read(UC_X86_REG_EAX)
assert call(0x37cc6a,this=P)==P
assert r(P+8)==0xffffffff and r(P+0xc)==0xffffffff and r(P+0x30)==0x3f and r(P+0x80)==0xfff
stages=[]
def stage(name):stages.append({'name':name,'dirty':r(P+0x30),'listener_effect_dirty':r(P+0x80),'effect_locations':[r(P+8),r(P+0xc)],'parameters_sha256':hashlib.sha256(bytes(u.mem_read(P,0xb4))).hexdigest()})
stage('original constructor')
assert call(0x37d506,[D+8,0x4043126f,0])==0
assert r(P+0x30)==0 and r(P+0x80)==0;stage('original immediate distance commit')
assert call(0x37d5cd,[D+8,0,0])==0
assert r(P+0x30)==0 and r(P+0x80)==0;stage('original immediate rolloff commit')
assert call(0x37d52a,[D+8,0,1])==0
assert r(P+0x30)==0x20 and r(P+0x80)==0;stage('original deferred Doppler')
w(0x903000,9,10);before=bytes(u.mem_read(P,0xb4));call(0x37a110,[0x903000],P);after=bytes(u.mem_read(P,0xb4))
assert r(P+8)==9 and r(P+0xc)==10 and all(x==y or 8<=i<16 for i,(x,y)in enumerate(zip(before,after)));stage('original download location helper')
assert call(0x37d598,[D+8,0,0,0,1])==0
assert call(0x37d54e,[D+8,0x3f800000,0,0,0,0x3f800000,0,1])==0
assert r(P+0x30)==0x25 and r(P+0x80)==0;stage('original deferred position/orientation')
assert call(0x37d141,[D+8])==0
assert r(P+0x30)==0 and r(P+0x80)==0;stage('original public commit with empty child list')
report={'passed':True,'stages':stages,'unique_original_addresses':len(ips),'no_substituted_functions':True,'no_mmio':True,'nonvolatile_esp_preserved':True,'scope':'Original parameter constructor, immediate scalar commits, deferred setters, exact location-only helper, public commit with empty list. IRQL2 and stereo config provided. Does not emulate DSP image upload or child processing.'}
(p/'listener-causal-result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
