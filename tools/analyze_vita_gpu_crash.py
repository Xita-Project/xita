#!/usr/bin/env python3
"""Read-only ARM ELF/GPU fault summary for Vita crash dumps.

Layouts: https://github.com/isage/vcp/blob/master/lib/src/info_gpuinfo.cpp
https://github.com/isage/vcp/blob/master/lib/src/info_memblockinfo.cpp
Register masks: GrapheneCt/PVR_PSP2 include/gpu_es4/eurasia/hwdefs/sgx543defs.h
Only the documented register tables are decoded; trailing GPU data is retained
as an undecoded byte count. A fault at an allocation boundary is a lead, not a
proof of which draw caused it or of a particular out-of-bounds access.
"""
import argparse,gzip,json,struct
from pathlib import Path

def unpack(fmt,data,offset):
 size=struct.calcsize(fmt)
 if offset<0 or offset+size>len(data):raise ValueError('truncated record')
 return struct.unpack_from(fmt,data,offset)

def notes(data):
 if data[:2]==b'\x1f\x8b':data=gzip.decompress(data)
 if data[:7]!=b'\x7fELF\x01\x01\x01':raise ValueError('expected little-endian ELF32')
 if unpack('<H',data,18)[0]!=40:raise ValueError('expected ARM ELF')
 phoff=unpack('<I',data,28)[0];entsize,count=unpack('<HH',data,42)
 if entsize!=32:raise ValueError('unexpected program header size')
 result={}
 for i in range(count):
  kind,offset,_,_,size,_,_,_=unpack('<8I',data,phoff+i*entsize)
  if kind!=4:continue
  end=offset+size
  if end>len(data):raise ValueError('truncated note segment')
  while offset<end:
   namesz,descsz,_=unpack('<3I',data,offset);offset+=12
   name_end=offset+namesz;desc_start=(name_end+3)&~3;desc_end=desc_start+descsz
   if desc_end>end:raise ValueError('truncated note')
   name=data[offset:name_end].rstrip(b'\0').decode('ascii')
   result[name]=data[desc_start:desc_end];offset=(desc_end+3)&~3
 return result

def analyze(n):
 b=n['GPU_INFO'];version,_,flags=unpack('<3I',b,0)
 cores,nmaster,ncore=unpack('<4I',b,0x2c)[:3]
 if version!=5 or cores!=4 or flags&3!=3:raise ValueError('unsupported GPU register layout')
 offset=0x3c;master={};per_core={}
 for _ in range(nmaster):
  addr,value=unpack('<2I',b,offset);offset+=8
  if addr in master:raise ValueError('duplicate master register')
  master[addr]=[value]
 for _ in range(ncore):
  addr,*values=unpack('<5I',b,offset);offset+=20
  if addr in per_core:raise ValueError('duplicate per-core register')
  per_core[addr]=values
 blocks=[];m=n.get('MEM_BLK_INFO',b'')
 if m:
  count=unpack('<I',m,4)[0]
  for i in range(count):
   start=8+i*72;unpack('<18I',m,start)
   name=m[start+8:start+40].split(b'\0')[0].decode(errors='replace')
   base,size=unpack('<2I',m,start+44)
   blocks.append(dict(name=name,start=base,bytes=size,end=base+size))
 faults=[]
 for kind,regs,status_addr,fault_addr in [('master',master,0x4c04,0x4c08),('core',per_core,0xc04,0xc08)]:
  if status_addr not in regs or fault_addr not in regs:raise ValueError('missing fault registers')
  for index,(status,raw) in enumerate(zip(regs[status_addr],regs[fault_addr])):
   request=status&0x3fff
   if not request:continue
   addr=raw&0xfffff000
   faults.append(dict(unit=kind,index=index,status=f'{status:08X}',request=f'{request:04X}',fault_type=(status>>16)&7,
    raw=f'{raw:08X}',page_address=f'{addr:08X}',client_id=raw&15,sideband=(raw>>4)&31,
    containing_blocks=[x for x in blocks if x['start']<=addr<x['end']],
    blocks_ending_at_page=[x for x in blocks if x['end']==addr]))
 return dict(gpu_version=version,gpu_cores=cores,master_registers=nmaster,per_core_registers=ncore,
  undecoded_gpu_bytes=len(b)-offset,faults=faults,
  tty_tail=n.get('TTY_INFO2',n.get('TTY_INFO',b''))[12:].decode(errors='replace').rstrip('\0'))

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('dump',type=Path);p.add_argument('--output',type=Path)
 args=p.parse_args();s=json.dumps(analyze(notes(args.dump.read_bytes())),indent=2)+'\n'
 if args.output:args.output.write_text(s)
 else:print(s,end='')
if __name__=='__main__':main()
