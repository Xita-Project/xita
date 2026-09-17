#!/usr/bin/env python3
"""Qualify the 86F9B vertex pass against independent owned-XBE instructions."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from recompiler import xita_recomp as r
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925 import collision_vertices

def generate(a):
    if not __debug__:raise ValueError('Assertions required')
    image=r.Image(str(a.xbe),str(a.manifest))
    assert hashlib.sha256(image.data).hexdigest()=='4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae'
    assert hashlib.sha256(image.bytes_at(0x86f50,884)).hexdigest()=='dac5ac8da738ab412fd265fb824a2a6abe9cde4f6fef19fb1a873ba5aba73d34'
    d=r.Discovery(image,{},image.kernel_imports(),lambda *args:None);d.add_root(0x86f50)
    f=d.functions[0x86f50];d.lift_function(f);d.split_blocks(f)
    body=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=NoGameHooks()).emit_function(f)
    region=body[body.index('L_00086F9B:'):body.index('L_0008709A:')]
    assert region.count('X_PREEMPT()')==4 and not __import__('re').search(r'\bf_[0-9A-F]+\(',region)
    assert collision_vertices.matches(image)
    hooked=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=HaloHooks(image)).emit_function(f)
    (a.out/'f_00086F50-original-private.c').write_text(body+'\n')
    (a.out/'f_00086F50-candidate-private.c').write_text(hooked+'\n')
    candidate_region=hooked[hooked.index('L_00086F9B:'):hooked.index('L_0008709A:')]
    assert candidate_region.count('xv_collision_vertices_begin()')==1
    prefix='#include "kernel/xk_collision_vertices.h"\n#undef X_G\n#define X_G(a) ((void *)(xram_+xpt_[(uint32_t)(a)>>12]+((uint32_t)(a)&4095u)))\n'
    roots='uint8_t *const xram_=g_xram;const uint32_t *const xpt_=g_xpt;\n'
    source=prefix+'void original_collision_vertices(xctx *restrict c){\n'+roots+region+'L_0008709A:return;\n}\nvoid candidate_collision_vertices(xctx *restrict c){\n'+roots+candidate_region+'L_0008709A:return;\n}\n'
    source+='void f_000B0CB0(xctx *);\n'+body.replace('void f_00086F50(', 'void original_full_vertices(',1)+'\n'+hooked.replace('void f_00086F50(', 'void candidate_full_vertices(',1)+'\n'
    read=image.bytes_at
    image.bytes_at=lambda addr,n: bytes(n) if (addr,n)==(0x86f50,884) else read(addr,n)
    assert not collision_vertices.matches(image)
    assert HaloHooks(image).transform_body(0x86f50,body)==body
    image.bytes_at=read
    assert HaloHooks(image).transform_body(0x86f9b,body)==body,'interior entry changed'
    if a.fp_model:
        header=(ROOT/'recomp/kernel/xk_collision_vertices.h').read_text()
        begin=header.index('static inline int xv_collision_vertices_fp_ok(void)')
        end=header.index('static inline __attribute__((always_inline)) unsigned xv_collision_vertices_begin',begin)
        assert '0x00009f00u' in header[begin:end]
        header=header[:begin]+'extern unsigned vertex_fp_block;\nstatic inline int xv_collision_vertices_fp_ok(void){return !vertex_fp_block;}\n'+header[end:]
        header=header.replace('#include "../xv_x86rt.h"','#include "xv_x86rt.h"')
        (a.out/'fp-admission-model-private.h').write_text(header)
        source=source.replace('#include "kernel/xk_collision_vertices.h"','#include "fp-admission-model-private.h"')
    path=a.out/'original-private.c';path.write_text(source)
    pp=[]
    for name,text in [('off-reference',body),('off-candidate',hooked)]:
        file=a.out/(name+'-private.c');file.write_text('#include "xv_x86rt.h"\n'+text)
        pp.append(subprocess.check_output([os.environ.get('CC','cc'),'-E','-P','-I'+str(ROOT/'recomp'),str(file)]))
    assert pp[0]==pp[1],'compile-OFF function changed'
    (a.out/'oracle.json').write_text(json.dumps({'image_sha256':hashlib.sha256(image.data).hexdigest(),'original_function_sha256':hashlib.sha256(body.encode()).hexdigest(),'original_fragment_sha256':hashlib.sha256((body+'\n').encode()).hexdigest(),'candidate_fragment_sha256':hashlib.sha256((hooked+'\n').encode()).hexdigest(),'region_sha256':hashlib.sha256(region.encode()).hexdigest(),'region':'86F9B..8709A'},indent=2)+'\n')
    return path

def arm(a,source):
    from test_arm_cluster_runtime import RuntimeMachine,RAM,PT,SIZE
    cc=os.environ.get('ARM_CC','/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
    flags=['-O2','-g','-std=gnu11','-fno-strict-aliasing','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-DTEST_ARM','-DXV_NATIVE_COLLISION_VERTICES','-I'+str(ROOT/'recomp'),'-ffunction-sections','-fdata-sections']
    commands=[];objects=[]
    for i,s in enumerate([source,ROOT/'tools/tests/collision_vertices.c',ROOT/'tools/tests/cluster_runtime_arm_imports.c',ROOT/'recomp/kernel/xk_collision_vertices_control.c']):
        obj=a.out/f'{i}.o';cmd=[cc,*flags,'-c',str(s),'-o',str(obj)];subprocess.run(cmd,check=True);commands.append(cmd);objects.append(str(obj))
    names=['arm_prepare','arm_original','arm_candidate','arm_off','arm_context_ptr','layout','vertex_yields','vertex_events','g_img_base','xv_collision_vertices_count','xv_collision_vertices_state']
    elf=a.out/'vertices.elf';cmd=[cc,*flags,*objects,'-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,'+','.join('--undefined='+n for n in names),'-lc','-lgcc','-o',str(elf)]
    subprocess.run(cmd,check=True);commands.append(cmd);(a.out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    m=RuntimeMachine(elf);m.imports={k:v for k,v in m.imports.items() if v!='__wrap_xv_preempt'}
    rows=[];base=[(n,v,b) for n in [1,3,8,32] for v in [0,1,4,8,128,256,512,3*512,4*512,6*512,7*512,32+4*512,48+4*512] for b in [1,100000]]
    base += [(8,(k<<18)|(k<<15)|(4<<9),1) for k in range(8)]
    specs=[(*s,0,1) for s in base]
    specs += [(4,64+((top%6)<<12)+(top<<15),100000,(rounding<<22)|control,1) for top in range(8) for rounding in range(4) for control in (0,0x01000000,0x02000000,0x0300009f)]
    specs += [(n,v,100000,0,0) for n in (1,3,8) for v in (0,1536,2048)]
    for n,v,b,fpscr,enabled in specs:
        m.call('arm_prepare',(n,v,b))
        if not enabled:m.call('arm_off')
        before=bytes(m.uc.mem_read(RAM,SIZE));pages=bytes(m.uc.mem_read(PT,4<<20));ctx=bytes(m.uc.mem_read(m.context,m.layout['size']))
        original=m.call('arm_original',fpscr=fpscr);expected=bytes(m.uc.mem_read(RAM,SIZE));epages=bytes(m.uc.mem_read(PT,4<<20));ectx=bytes(m.uc.mem_read(m.context,m.layout['size']))
        ey=bytes(m.uc.mem_read(m.symbols['vertex_yields'],4));eh=bytes(m.uc.mem_read(m.symbols['vertex_events'],4))
        m.uc.mem_write(RAM,before);m.uc.mem_write(PT,pages);m.uc.mem_write(m.context,ctx)
        m.uc.mem_write(m.symbols['vertex_yields'],bytes(4));m.uc.mem_write(m.symbols['vertex_events'],struct.pack('<I',2166136261))
        candidate=m.call('arm_candidate',fpscr=fpscr)
        checks={'context':bytes(m.uc.mem_read(m.context,m.layout['size']))==ectx,'memory':bytes(m.uc.mem_read(RAM,SIZE))==expected,'pages':bytes(m.uc.mem_read(PT,4<<20))==epages,'yield_count':bytes(m.uc.mem_read(m.symbols['vertex_yields'],4))==ey,'yield_observations':bytes(m.uc.mem_read(m.symbols['vertex_events'],4))==eh,'fpscr':original['fpscr']==candidate['fpscr']}
        checks['admitted_once']=struct.unpack('<I',m.uc.mem_read(m.symbols['xv_collision_vertices_count'],4))[0]==enabled
        checks['scope_retired']=struct.unpack('<I',m.uc.mem_read(m.symbols['xv_collision_vertices_state'],4))[0]==enabled
        row={'n':n,'variant':v,'budget':b,'fpscr':fpscr,'enabled':enabled,'original':original,'candidate':candidate,'checks':checks,'yields':struct.unpack('<I',ey)[0]}
        if not all(checks.values()):
            (a.out/'failure.json').write_text(json.dumps(row,indent=2)+'\n');raise AssertionError(row)
        rows.append(row);print('PASS ARM vertices',n,v,b,original['instructions'],candidate['instructions'],flush=True)
    (a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n');print('PASS',len(rows),'ARM comparisons')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['xbe','manifest','out']:p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--sanitize',action='store_true');p.add_argument('--arm',action='store_true');p.add_argument('--fp-model',action='store_true',help='Host-only model of unavailable native trap controls; tests exact continuation branches')
    p.add_argument('--emit-only',action='store_true',help='Validate the owned image and emit private full-function fragments without executing fixtures')
    a=p.parse_args()
    if a.arm and a.fp_model:p.error('--fp-model is host only')
    a.out.mkdir(parents=True,exist_ok=False);source=generate(a)
    if a.emit_only:return
    if a.arm:return arm(a,source)
    cmd=[os.environ.get('CC','cc'),'-O1' if a.sanitize else '-O2','-g1','-std=gnu11','-fno-strict-aliasing','-DXV_NATIVE_COLLISION_VERTICES','-I'+str(ROOT/'recomp'),*(['-DCOLLISION_VERTICES_FP_MODEL'] if a.fp_model else []),*(['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'] if a.sanitize else []),str(source),str(ROOT/'tools/tests/collision_vertices.c'),str(ROOT/'recomp/kernel/xk_collision_vertices_control.c'),'-Wl,--wrap=xv_preempt','-lm','-pthread','-o',str(a.out/'test')]
    (a.out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n');subprocess.run(cmd,check=True);subprocess.run([str(a.out/'test')],check=True,timeout=120)
if __name__=='__main__':main()
