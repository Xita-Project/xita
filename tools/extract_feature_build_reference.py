#!/usr/bin/env python3
"""Extract an owned-image reference closure for collision feature construction.

Generated guest code must stay in a private output directory, never committed.
Reject indirect/HLE dependencies rather than silently stubbing engine work.
This prepares a differential oracle; it is not a native replacement.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from recompiler import xita_recomp as r
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925.discovery import HaloDiscovery

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--xbe',required=True);ap.add_argument('--manifest',required=True)
    ap.add_argument('--output-dir',required=True,type=Path)
    a=ap.parse_args()
    output=a.output_dir.resolve()
    if output==ROOT or ROOT in output.parents:
        ap.error('generated reference must be outside the source repository')
    img=r.Image(a.xbe,a.manifest)
    discovery=HaloDiscovery(img,{},img.kernel_imports(),lambda *args:None)
    pending=[0x868F0];bodies={};records=[]
    while pending:
        address=pending.pop()
        if address in bodies:continue
        if len(bodies)>=64:raise ValueError('unexpectedly large feature closure')
        discovery.add_root(address)
        function=discovery.functions[address]
        discovery.lift_function(function);discovery.split_blocks(function)
        body=r.Emitter(img,discovery,{},img.kernel_imports(),'unused',1,hooks=NoGameHooks()).emit_function(function)
        if re.search(r'\bxv_call\s*\(|\bxk_hle\w*\s*\(',body):
            raise ValueError(f'unresolved indirect/HLE dependency at {address:08X}')
        callees=sorted({int(n,16) for n in re.findall(r'\bf_([0-9A-Fa-f]{8})\(c\)',body)})
        bodies[address]=body;pending.extend(n for n in callees if n not in bodies)
        records.append({'address':f'{address:08X}','callees':[f'{n:08X}' for n in callees],
                        'body_sha256':hashlib.sha256(body.encode()).hexdigest()})
    output.mkdir(parents=True,exist_ok=False)
    text='#include "xv_x86rt.h"\n'
    text+=''.join(f'void f_{address:08X}(xctx *restrict c);\n' for address in sorted(bodies))
    text+='\n'.join(bodies[address] for address in sorted(bodies))
    (output/'reference.c').write_text(text)
    # Absolute float constants and the indexed 3-axis/two-sign projection table.
    constants=sorted({int(v,16) for v in re.findall(r'ds:\[([0-9A-F]+)h\]',text)})
    doubles={int(v,16) for v in re.findall(r'qword ptr ds:\[([0-9A-F]+)h\]',text)}
    ranges=[(address,8 if address in doubles else 4) for address in constants]+[(0x1EAF30,24)]
    lines=['/* Private owned-image fixture constants. Do not commit. */',
           'static void reference_init_constants(void) {']
    for address,size in ranges:
        data=img.bytes_at(address,size)
        if len(data)!=size:raise ValueError(f'missing constant {address:08X}')
        values=','.join(f'0x{v:02x}' for v in data)
        lines.append(f'  {{ const unsigned char bytes[]={{{values}}}; memcpy(g_xram+0x{address:x},bytes,sizeof bytes); }}')
    lines.append('}')
    (output/'reference_constants.h').write_text('\n'.join(lines)+'\n')
    (output/'manifest.json').write_text(json.dumps({'root':'000868F0','xbe_sha256':hashlib.sha256(img.data).hexdigest(),
        'constant_ranges':[{'address':f'{address:08X}','size':size,
                            'sha256':hashlib.sha256(img.bytes_at(address,size)).hexdigest()} for address,size in ranges],
        'functions':sorted(records,key=lambda r:r['address']),
        'note':'Owned executable lift without game hooks. Reference only; no speed or native correctness result.'},indent=2)+'\n')
    print(f'Extracted {len(bodies)} reference functions into private output')

if __name__=='__main__':main()
