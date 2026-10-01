#!/usr/bin/env python3
"""Compare the native child hierarchy against independent owned-XBE lifts."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from recompiler import xita_recomp as r
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925.discovery import HaloDiscovery


def region(body,name):
    prefix=body[:body.index('L_0008DDF0:')].replace('f_0008DDF0',name)
    start=body.index('L_0008E0F0:')
    end=body.index('L_0008E5D0:',start)
    return prefix+body[start:end]+'L_0008E5D0:\n    return;\n}\n'


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--xbe',required=True);ap.add_argument('--manifest',required=True)
    ap.add_argument('--output-dir',type=Path,required=True,help='Private evidence directory; never publish generated game code')
    ap.add_argument('--root-chain',action='store_true',help='Exercise native root-chain uptake in the complete lifted hierarchy')
    a=ap.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
    image=r.Image(a.xbe,a.manifest);hooks=HaloHooks(image);assert hooks.hierarchy_enabled
    read=image.bytes_at
    for address,size in ((0x8DDF0,2218),(0xB5B40,339),(0xB5F60,291)):
        def changed(p,n):
            data=bytearray(read(p,n))
            if (p,n)==(address,size):data[-1]^=1
            return bytes(data)
        image.bytes_at=changed
        try:assert not HaloHooks(image).before_instruction(0x8E0F0)
        finally:image.bytes_at=read
    d=HaloDiscovery(image,{},image.kernel_imports(),lambda *args:None)
    for address in (0x8DDF0,0xB5B40,0xB5F60):
        d.add_root(address);d.lift_function(d.functions[address]);d.split_blocks(d.functions[address])
    original=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=NoGameHooks())
    candidate=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=hooks)
    reference=region(original.emit_function(d.functions[0x8DDF0]),'original_hierarchy')
    hooked=region(candidate.emit_function(d.functions[0x8DDF0]),'candidate_hierarchy')
    assert hooked.count('(void)xv_math_model_hierarchy(c)')==1
    cc=os.environ.get('CC','cc');include='-I'+str(ROOT/'recomp')
    with tempfile.TemporaryDirectory(prefix='xita-hierarchy-') as temp:
        temp=Path(temp);preprocessed=[]
        for label,body in (('original',reference),('candidate',hooked)):
            path=temp/(label+'.c');path.write_text('#include "xv_x86rt.h"\n'+body.replace(label+'_hierarchy','compare_hierarchy'))
            preprocessed.append(subprocess.check_output([cc,'-E','-P',include,str(path)]))
        assert preprocessed[0]==preprocessed[1], 'Disabled experiment changes the translated region'
    leaves=''
    for address,name in ((0xB5B40,'matrix'),(0xB5F60,'quaternion')):
        leaves+=original.emit_function(d.functions[address]).replace(f'f_{address:08X}','original_'+name)
        leaves+=candidate.emit_function(d.functions[address])
    source=a.output_dir/'original.c'
    source.write_text('#include "xv_x86rt.h"\n'+leaves+
        reference.replace('f_000B5B40','original_matrix').replace('f_000B5F60','original_quaternion')+
        reference.replace('original_hierarchy','current_hierarchy')+hooked)
    flags=['-std=gnu11','-fno-strict-aliasing','-ffp-contract=off','-DXV_NATIVE_MODEL_HIERARCHY',
           '-ffunction-sections','-fdata-sections',include,*shlex.split(os.environ.get('NATIVE_MATH_CFLAGS',''))]
    if a.root_chain:
        flags+=['-DXV_ROOT_CHAIN_TEST','-Wl,--wrap=xv_math_root_chain']
        os.environ['XV_ROOT_CHAIN']='1'
    sources=[source,ROOT/'tools/tests/model_hierarchy.c',ROOT/'recomp/kernel/xk_hierarchy.c',
             ROOT/'recomp/kernel/xk_math.c',ROOT/'recomp/xv_x86rt.c']
    objects=[]
    for i,path in enumerate(sources):
        obj=a.output_dir/f'host-{i}.o';objects.append(str(obj))
        opt='-O3' if path.name in ('xk_hierarchy.c','xk_math.c') else '-O2'
        subprocess.run([cc,*flags,opt,'-c',str(path),'-o',str(obj)],check=True)
    binary=a.output_dir/'host-test'
    subprocess.run([cc,*flags,*objects,'-Wl,--gc-sections,--wrap=xv_preempt','-lm','-o',str(binary)],check=True)
    for mode in ('enabled','unset','disabled','math-disabled'):
        subprocess.run([str(binary),mode],check=True)
    print('PASS: hierarchy signatures, inactive translation, guest-state and memory comparisons')


if __name__=='__main__':main()
