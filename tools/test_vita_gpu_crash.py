#!/usr/bin/env python3
"""Check register table bounds, active fault masks and boundary attribution."""
import struct,gzip
from analyze_vita_gpu_crash import analyze,notes
b=bytearray(0x3c);struct.pack_into('<3I',b,0,5,0x10003,0x3f);struct.pack_into('<3I',b,0x2c,4,2,2)
b+=struct.pack('<4I',0x4c04,0x80000,0x4c08,0)
b+=struct.pack('<5I',0xc04,0x80000,0x80000,0x80000,0x90400)
b+=struct.pack('<5I',0xc08,0,0,0,0x70400006)
m=bytearray(8+144);struct.pack_into('<2I',m,0,1,2)
for i,(name,base,size) in enumerate([(b'xv_vertex_ring',0x70200000,0x200000),(b'SceGxmRenderTarget',0x70400000,0x7e000)]):
 at=8+i*72;m[at+8:at+8+len(name)]=name;struct.pack_into('<2I',m,at+44,base,size)
n={'GPU_INFO':bytes(b),'MEM_BLK_INFO':bytes(m)};r=analyze(n);assert len(r['faults'])==1
f=r['faults'][0];assert f['page_address']=='70400000' and f['index']==3
assert f['blocks_ending_at_page'][0]['name']=='xv_vertex_ring';assert f['containing_blocks'][0]['name']=='SceGxmRenderTarget'
for i in [0,10,59,len(b)-1]:
 try:analyze({'GPU_INFO':bytes(b[:i])})
 except (ValueError,KeyError):pass
 else:raise AssertionError('accepted truncated registers')
# Exercise the actual ELF note reader, including gzip and 4-byte name padding.
body=b''
for name,data in n.items():
 name=name.encode()+b'\0';body+=struct.pack('<3I',len(name),len(data),0)+name+bytes((-len(name))%4)+data+bytes((-len(data))%4)
elf=bytearray(84);elf[:7]=b'\x7fELF\x01\x01\x01';struct.pack_into('<H',elf,18,40);struct.pack_into('<I',elf,28,52);struct.pack_into('<HH',elf,42,32,1);struct.pack_into('<8I',elf,52,4,84,0,0,len(body),0,0,4);elf+=body
assert notes(bytes(elf))==n and notes(gzip.compress(elf))==n
try:notes(bytes(elf[:-1]))
except ValueError:pass
else:raise AssertionError('accepted truncated note segment')
print('GPU crash parser: fault masks, register boundaries, allocation boundaries, ELF/gzip and truncation checks passed')
