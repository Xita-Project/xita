#!/usr/bin/env python3
"""Actual ARM uploader entry counts; modeled firmware work is separate, never cycles/FPS."""
import argparse,hashlib,json,subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE,UC_MEM_READ
from unicorn.arm_const import *
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--wide-compare',action='store_true');p.add_argument('--sdk',type=Path,default=Path.home()/'vitasdk');a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
 rows=[];elf_hashes={}
 for compiled,compact in ((0,0),(1,0),(1,1)):
  elfpath=out/f'upload-{compiled}-{compact}.elf'
  subprocess.run([str(a.sdk/'bin/arm-vita-eabi-gcc'),'-O2','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-fno-strict-aliasing','-ffunction-sections','-fdata-sections',f'-DXV_PACKED_VERTEX_LAYOUT={compiled}',f'-DXV_VERTEX_WIDE_COMPARE={int(a.wide_compare)}',f'-DXV_VERTEX_CAPTURE_PACKED={compact}','-I'+str(ROOT),'-I'+str(ROOT/'runtime'),'-nostdlib',str(ROOT/'tools/tests/packed_vertex_arm.c'),'-Wl,--gc-sections,-Ttext=0x10000,-e,test_call,-u,test_setup','-lc','-lgcc','-o',str(elfpath)],check=True)
  elf_hashes[f'{compiled}-{compact}']=hashlib.sha256(elfpath.read_bytes()).hexdigest()
  u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9);u.reg_write(UC_ARM_REG_C1_C0_2,15<<20);u.reg_write(UC_ARM_REG_FPEXC,1<<30)
  images=[];writable=[]
  with elfpath.open('rb') as f:
   e=ELFFile(f);syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};pages=set()
   for seg in e.iter_segments():
    if seg['p_type']!='PT_LOAD':continue
    lo,n=seg['p_vaddr'],seg['p_memsz'];images.append((lo,lo+n))
    if seg['p_flags']&2:writable.append((lo,lo+n))
    for page in range(lo&~4095,(lo+n+4095)&~4095,4096):
     if page not in pages:u.mem_map(page,4096);pages.add(page)
    u.mem_write(lo,seg.data())
  A,B,G,S,END,CAP=0x200000,0x400000,0x600000,0x800000,0x900000,0x40000
  for addr,n in [(A,CAP),(B,CAP),(G,CAP),(S,65536),(END,4096)]:u.mem_map(addr,n)
  state={'count':0,'firmware_copy':0,'firmware_fill':0,'reads':0,'writes':0,'measure':False};allowed_read=[];allowed_write=[]
  def access(uc,kind,addr,n,value,ctx):
   ranges=(images+allowed_read) if kind==UC_MEM_READ else (writable+allowed_write)
   assert any(lo<=addr and addr+n<=hi for lo,hi in ranges+[(S,S+65536)]),(kind,hex(addr),n,allowed_read,allowed_write)
   if state['measure']:
    if A<=addr<A+CAP:state['reads']+=n
    if B<=addr<B+CAP and kind!=UC_MEM_READ:state['writes']+=n
  u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,access)
  def code(uc,addr,n,ctx):
   if state['measure']:state['count']+=1
   for name,key in [('sceClibMemcpy','firmware_copy'),('sceClibMemset','firmware_fill')]:
    if name in syms and addr==(syms[name]&~1):
     dst,src,size=[uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)]
     if not size:return
     access(uc,17,dst,size,0,None)
     if key=='firmware_copy':access(uc,UC_MEM_READ,src,size,0,None);data=bytes(uc.mem_read(src,size))
     else:data=bytes([src&255])*size
     uc.mem_write(dst,data)
     if state['measure']:state[key]+=size
  u.hook_add(UC_HOOK_CODE,code)
  def invoke(name,args=()):
   for reg,value in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):u.reg_write(reg,value)
   u.reg_write(UC_ARM_REG_SP,S+65024);u.reg_write(UC_ARM_REG_LR,END|1);u.emu_start(syms[name],END,count=3000000)
   assert u.reg_read(UC_ARM_REG_PC)==END,(name,hex(u.reg_read(UC_ARM_REG_PC)))
   return u.reg_read(UC_ARM_REG_R0)
  for packed in range(3 if compact else compiled+1):
   for nv in (1,2,3,4,8,16,32,64,128,256,512,1024,1485,2123):
    for scenario in ('resident','cached','first-miss','last-miss','unused-tail'):
     for offset in (0,1):
      source=bytes((i*13+(i//32)*17)&255 for i in range(nv*32));old=bytearray(source)
      if scenario=='first-miss':old[0]^=1
      if scenario=='last-miss':old[-17]^=1
      if scenario=='unused-tail':old[-1]^=1
      mirror=b''.join(old[i:i+16] for i in range(0,len(old),32)) if packed else bytes(old)
      want=b''.join(source[i:i+16] for i in range(0,len(source),32)) if packed else source
      if packed==2:source=want
      allowed_read=[(A+offset,A+offset+len(source)),(B,B+2*len(mirror)),(G,G+2*len(mirror))];allowed_write=[(B,B+2*len(mirror)),(G,G+2*len(mirror))]
      # Warm controls once then reset all inputs/pool metadata for the count.
      for measured in (False,True):
       state.update(count=0,firmware_copy=0,firmware_fill=0,reads=0,writes=0,measure=False)
       u.mem_write(A+offset,source);u.mem_write(B,mirror+bytes(len(mirror)))
       invoke('test_setup',(A+offset,B,G,(nv<<8)|(packed<<4)|(scenario=='cached')))
       state['measure']=measured;result=invoke('test_call');state['measure']=False
       assert result==G,(nv,scenario,packed,hex(result))
       assert bytes(u.mem_read(B,len(want)))==want,(compiled,packed,nv,scenario)
      rows.append(dict(compiled=compiled,compact_compiled=compact,packed=packed,vertices=nv,source_bytes=len(source),scenario=scenario,alignment=offset,**{k:v for k,v in state.items() if k!='measure'}))
 report=dict(result='PASS',wide_compare=a.wide_compare,rows=rows,elf_sha256=elf_hashes,scope='Complete production uploader entry, warm config, residency and grouped comparisons ON. Queued GPU-copy execution excluded; modeled firmware bytes separate from instruction counts. Bounds and exact CPU snapshot checked. Not cycles/cache misses/FPS.')
 (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PASS',len(rows),'actual ARM uploader cases; complete counts and firmware bytes saved')
if __name__=='__main__':
 if not __debug__:raise SystemExit('Run without Python -O')
 main()
