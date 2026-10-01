#!/usr/bin/env python3
"""Verify owned-image root pair admission and disabled generated code identity."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from recompiler import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925.discovery import HaloDiscovery
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--xbe',required=True);p.add_argument('--manifest',required=True)
a=p.parse_args()
image=r.Image(a.xbe,a.manifest);hooks=HaloHooks(image)
assert hooks.hierarchy_enabled
read=image.bytes_at
for address,size in ((0x8DDF0,2218),(0xB5B40,339),(0xB5F60,291)):
    def changed(p,n):
        data=bytearray(read(p,n))
        if (p,n)==(address,size):data[-1]^=1
        return bytes(data)
    image.bytes_at=changed
    try:assert not HaloHooks(image).hierarchy_enabled
    finally:image.bytes_at=read
d=HaloDiscovery(image,{},image.kernel_imports(),lambda *args:None)
d.add_root(0x8DDF0);d.lift_function(d.functions[0x8DDF0]);d.split_blocks(d.functions[0x8DDF0])
emitter=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=hooks)
enabled=emitter.emit_function(d.functions[0x8DDF0])
assert enabled.count('if (xv_math_root_pair(c)) goto L_0008E58B;')==1
assert enabled.count('if (xv_math_root_chain(c)) goto L_0008E58B;')==2
hooks.hierarchy_enabled=False
reference=emitter.emit_function(d.functions[0x8DDF0])
assert 'xv_math_root_pair' not in reference
assert 'xv_math_root_chain' not in reference
# Both hierarchy hooks are compiled out in the baseline configuration.
with tempfile.TemporaryDirectory(prefix='xita-root-hook-') as directory:
    outputs=[]
    for name,body in [('enabled',enabled),('reference',reference)]:
        path=Path(directory)/(name+'.c');path.write_text('#include "xv_x86rt.h"\n'+body)
        outputs.append(subprocess.check_output(['cc','-E','-P','-I'+str(ROOT/'recomp'),str(path)]))
    assert outputs[0]==outputs[1]
print('PASS exact-image root hook; three changed-image guards; disabled code identity')
