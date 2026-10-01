#!/usr/bin/env python3
"""Owned-image hook placement, signature rejection and disabled-code identity."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from recompiler import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925.discovery import HaloDiscovery
from games.halo_ce_3925 import feature_vertices
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--xbe',required=True);p.add_argument('--manifest',required=True)
a=p.parse_args();image=r.Image(a.xbe,a.manifest);hooks=HaloHooks(image)
d=HaloDiscovery(image,{},image.kernel_imports(),lambda *args:None)
d.add_root(0x868F0);d.lift_function(d.functions[0x868F0]);d.split_blocks(d.functions[0x868F0])
emitter=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=hooks)
body=emitter.emit_function(d.functions[0x868F0])
call='if (xv_native_feature_vertices(c, f_000855F0, f_000862A0, f_00086170)) return;'
assert body.count(call)==1
assert body.index('cleanup(xv_object_motion_end)')<body.index(call)<body.index('L_000868F0:')
with patch.object(feature_vertices,'entry',return_value=[]):reference=emitter.emit_function(d.functions[0x868F0])
read=image.bytes_at
pc,size,_=feature_vertices.SPAN
for offset in (0,size-1):
 def changed(address,n):
  data=bytearray(read(address,n))
  if (address,n)==(pc,size):data[offset]^=1
  return bytes(data)
 image.bytes_at=changed
 assert feature_vertices.entry(image,pc)==[]
image.bytes_at=read
with tempfile.TemporaryDirectory(prefix='xita-feature-hook-') as directory:
 outputs=[]
 for name,code in [('hook',body),('reference',reference)]:
  path=Path(directory)/(name+'.c');path.write_text('#include "xv_x86rt.h"\n'+code)
  outputs.append(subprocess.check_output(['cc','-E','-P','-I'+str(ROOT/'recomp'),str(path)]))
 assert outputs[0]==outputs[1]
print('PASS feature hook: exact signature, observer before hook, disabled code identity')
