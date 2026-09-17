#!/usr/bin/env python3
"""Bounded private SSE-distance reconstruction with a strict whole-query oracle.

Consumes retained selective-inline cost artifacts, using their actual production
compiler command and unchanged generic/fixture objects. No generated guest code
is committed. All owned outputs must remain outside the source worktree.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT)]
from test_arm_cluster_runtime import RuntimeMachine,RAM,SIZE,STACK
from unicorn.arm_const import UC_ARM_REG_SP

ORIGINAL='''    for(unsigned i=0;i<4;i++)c->xmm[0][i]-=c->xmm[1][i];
    for(unsigned i=0;i<4;i++)c->xmm[0][i]*=c->xmm[0][i];
    c->xmm[2][0]=c->xmm[0][0];
    x_shufps(c,c->xmm[0],c->xmm[0],14);
    c->xmm[2][0]+=c->xmm[0][0];
    x_shufps(c,c->xmm[0],c->xmm[0],57);
    c->xmm[2][0]+=c->xmm[0][0];X_MF32(q0)=c->xmm[2][0];'''
REPLACEMENT='''    nq_semantic_vertex_distance(c);
    X_MF32(q0)=c->xmm[2][0];'''
HEADER=ROOT/'tools/tests/query_semantic_leaf.h'
# Pins the reviewed VFP operand order as well as source/caller eligibility.
RETAINED_TEXT_SHA256='dedc7a8c7aab35f03bb14d074e23c852208b5715f6deadef7b9a5f604478ad98'

def sha(data):return hashlib.sha256(data).hexdigest()
def state_recipe():
    lanes=['xx','padding','yy','zz']
    def shuffle(values,imm):
        return [values[imm&3],values[(imm>>2)&3],values[(imm>>4)&3],values[(imm>>6)&3]]
    first=shuffle(lanes,14);final=shuffle(first,57)
    assert first==['yy','zz','xx','xx'] and final==['zz','xx','xx','yy']
    assert ORIGINAL.count('X_MF32(q0)=c->xmm[2][0]')==1
    assert REPLACEMENT.count('X_MF32(q0)=c->xmm[2][0]')==1
    assert not any(token in ORIGINAL for token in ('X_PREEMPT','X_PUSH32','X_POP32','x87_'))
    return dict(xmm0=final,xmm1='unchanged',xmm2_low='(yy+xx)+zz with retained VFP operand order',
                xmm2_high='unchanged',other_context='unchanged',guest_stores=['X_MF32(q0)=xmm2.low'],
                observation_points_inside_interval=0,fp_ops=['sub32']*4+['mul32']*4+['add32']*2)
def transform(source,enabled):
    if source.count(ORIGINAL)!=1:raise ValueError('typed vertex interval drift')
    if source.index(ORIGINAL)<source.index('NQ_CV_vertex:'):raise ValueError('wrong vertex interval')
    if not enabled:return source
    marker='__attribute__((noinline)) void query_fused_172c95_171f94('
    if source.count(marker)!=1:raise ValueError('query entry drift')
    changed=source.replace(ORIGINAL,REPLACEMENT)
    changed=changed.replace(marker,'#include "'+str(HEADER)+'"\n'+marker)
    restored=changed.replace('#include "'+str(HEADER)+'"\n','').replace(REPLACEMENT,ORIGINAL)
    if restored!=source:raise ValueError('nonlocal source edit')
    return changed

def allocated(path):
    with path.open('rb') as f:
        elf=ELFFile(f)
        sections={s.name:s.data().hex() for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']!='SHT_NOBITS'}
        relocs={}
        for section in elf.iter_sections():
            if section['sh_type'] not in ('SHT_REL','SHT_RELA'):continue
            target=elf.get_section(section['sh_info'])
            if not(target['sh_flags']&2):continue
            symbols=elf.get_section(section['sh_link'])
            relocs[section.name]=[(r['r_offset'],r['r_info_type'],symbols.get_symbol(r['r_info_sym']).name) for r in section.iter_relocations()]
        return sections,relocs

def main():
    if not __debug__:raise SystemExit('Refusing optimized Python')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cost-dir',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--build-only',action='store_true')
    a=p.parse_args();a.out=a.out.resolve();a.cost_dir=a.cost_dir.resolve()
    if a.out.is_relative_to(ROOT):p.error('owned output must stay outside source worktree')
    a.out.mkdir(parents=True,exist_ok=False)
    source=(a.cost_dir/'inline.c').read_text()
    original_command=shlex.split(next(line for line in (a.cost_dir/'inline-make.log').read_text().splitlines() if ' -c recomp/query_fusion.c ' in line))
    commands=[]
    for lane in ('baseline','inline'):
        candidate=transform(source,lane=='inline')
        path=a.out/(lane+'.c');path.write_text(candidate)
        command=list(original_command)
        command[command.index('-c')+1]=str(path)
        command[command.index('-o')+1]=str(a.out/(lane+'.o'))
        subprocess.run(command,cwd=a.cost_dir/'build',check=True)
        commands.append(dict(command=command,cwd=str(a.cost_dir/'build')))
        (a.out/(lane+'-make.log')).write_text(shlex.join(command)+'\n')
    if sha(bytes.fromhex(allocated(a.cost_dir/'inline.o')[0]['.text']))!=RETAINED_TEXT_SHA256:
        raise AssertionError('retained distance operand-order baseline requires fresh review')
    if allocated(a.out/'baseline.o')!=allocated(a.cost_dir/'inline.o'):
        raise AssertionError('baseline production object allocated sections/relocations changed')
    links=json.loads((a.cost_dir/'execution-commands.json').read_text())
    saved=[links[0],links[1]]
    shutil.copy2(a.cost_dir/'reference.elf',a.out/'reference.elf')
    for lane in ('baseline','inline'):
        command=list(links[3]);old=str(a.cost_dir/'inline.o')
        assert command.count(old)==1
        command[command.index(old)]=str(a.out/(lane+'.o'))
        command[-1]=str(a.out/(lane+'.elf'))
        subprocess.run(command,check=True);saved.append(command)
    (a.out/'execution-commands.json').write_text(json.dumps(saved,indent=2)+'\n')
    # Observer runner expects build-relative include paths; point only its
    # compilation cwd at the immutable retained headers, never mutate them.
    (a.out/'build').symlink_to(a.cost_dir/'build',target_is_directory=True)
    reports={}
    cc=original_command[0];nm=cc.removesuffix('gcc')+'nm';objdump=cc.removesuffix('gcc')+'objdump'
    for lane in ('baseline','inline'):
        obj=a.out/(lane+'.o')
        with (a.out/(lane+'.asm')).open('w') as stream:subprocess.run([objdump,'-dr',str(obj)],stdout=stream,check=True)
        sec,_=allocated(obj)
        reports[lane]=dict(text_bytes=len(bytes.fromhex(sec['.text'])),text_sha256=sha(bytes.fromhex(sec['.text'])),
            symbols=subprocess.check_output([nm,'-S',str(obj)],text=True),
            imports=subprocess.check_output([nm,'-u',str(obj)],text=True),stack=(a.out/(lane+'.su')).read_text())
    (a.out/'build.json').write_text(json.dumps(dict(commands=commands,results=reports,
        exact_baseline_allocated_sections_and_relocations=True,source_sha256=sha(source.encode()),
        authored_header_sha256=sha(HEADER.read_bytes()),original_interval_sha256=sha(ORIGINAL.encode()),
        on_change='one arithmetic interval; original distance store retained',off_identical=transform(source,False)==source,state_recipe=state_recipe()),indent=2)+'\n')
    if a.build_only:return
    class Machine(RuntimeMachine):
        def step(self,uc,address,size,user):
            self.min_sp=min(self.min_sp,uc.reg_read(UC_ARM_REG_SP));super().step(uc,address,size,user)
        def call(self,*args,**kw):
            self.min_sp=0xffffffff;result=super().call(*args,**kw)
            result['peak_stack_bytes']=STACK+65024-self.min_sp;return result
    machines={lane:Machine(a.out/(lane+'.elf'),profile=True) for lane in ('reference','baseline','inline')}
    for m in machines.values():m.imports={k:v for k,v in m.imports.items() if v!='__wrap_xv_preempt'}
    def state(m):
        result=dict(context=bytes(m.uc.mem_read(m.context,m.layout['size'])),memory=bytes(m.uc.mem_read(RAM,SIZE)))
        for name,length in [('ct_yields',4),('ct_events',4),('ct_seen',4),('ct_boundary',4),('ct_original_pages',4096),('ct_alternate_pages',4096)]:
            result[name]=bytes(m.uc.mem_read(m.symbols[name],length))
        for name in ('g_xram','g_img_base','g_xpt'):
            value=struct.unpack('<I',m.uc.mem_read(m.symbols[name],4))[0]
            result[name]='original' if name=='g_xpt' and value==m.symbols['ct_original_pages'] else 'alternate' if name=='g_xpt' and value==m.symbols['ct_alternate_pages'] else value
        return result
    rows=[]
    for depth in (1,16):
        for variant in (0,3,4,8,12,15,1<<25):
            expected=None;result={}
            for lane,m in machines.items():
                m.call('arm_prepare',(depth,variant,100000))
                stats=m.call('arm_original' if lane=='reference' else 'arm_candidate')
                actual=state(m)
                if expected is None:expected=actual;fp=stats['fpscr']
                checks={key:actual[key]==value for key,value in expected.items()};checks['fpscr']=stats['fpscr']==fp
                if not all(checks.values()):raise AssertionError((lane,depth,variant,checks))
                result[lane]=dict(stats=stats,checks=checks)
            rows.append(dict(depth=depth,variant=variant,lanes=result))
            (a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
            print('PASS whole query',depth,variant,{lane:r['stats']['instructions'] for lane,r in result.items()},flush=True)

if __name__=='__main__':main()
