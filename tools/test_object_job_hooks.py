#!/usr/bin/env python3
"""Check experimental object hooks against an owned supported executable."""
import argparse
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root))
from games.halo_ce_3925.hooks import HaloHooks
from recompiler import xita_recomp as r

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--xbe',required=True)
ap.add_argument('--manifest',required=True)
args=ap.parse_args()
img=r.Image(args.xbe,args.manifest)
hooks=HaloHooks(img)
assert hooks.enabled and hooks.object_scan_enabled
for address,call in ((0x900E0,'xv_object_jobs_begin(c)'),
                     (0x902A9,'xv_object_jobs_join()'),
                     (0x90314,'xv_object_jobs_finish(c)')):
    lines=hooks.before_instruction(address)
    assert lines[0]=='#ifdef XV_EXPERIMENTAL_OBJECT_JOBS' and call in '\n'.join(lines)
assert 'xv_object_jobs_queue(c)' in '\n'.join(hooks.function_entry(0x8FB70))
read=img.bytes_at
def changed(address,size):
    data=bytearray(read(address,size))
    if (address,size) in ((0x8FB70,0x111),(0x900E0,0x239)):data[-1]^=1
    return bytes(data)
img.bytes_at=changed
modified=HaloHooks(img)
assert not modified.object_scan_enabled
assert 'xv_object_jobs_queue' not in '\n'.join(modified.function_entry(0x8FB70))
for address in (0x900E0,0x902A9,0x90314):assert not modified.before_instruction(address)
print('PASS: exact object callback and pass signatures; instruction hooks cover inlined copies; modified bodies decline')
