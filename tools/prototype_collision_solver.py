#!/usr/bin/env python3
"""Private whole-call solver prototype; never edits generated production units.

Owned instruction bodies are checked and written only under --out. The existing
actor guard is untouched. This is not the unlock experiment.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from games.halo_ce_3925 import collision_solver
from games.halo_ce_3925.hooks import HaloHooks
from recompiler import xita_recomp as r

IMAGE = '4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae'
ENTRIES = tuple(a for a, _, _ in collision_solver.SPANS if a != 0x172BF0)
PROFILE = '''#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_HOLD_PROFILE)
    extern unsigned xv_object_hold_children_enabled;
    extern unsigned xv_object_motion_begin(xctx *, unsigned);
    extern void xv_object_motion_end(unsigned *);
    unsigned motion_sample_ __attribute__((cleanup(xv_object_motion_end))) =
        xv_object_hold_children_enabled ? xv_object_motion_begin(c, 4u) : 0;
#endif
'''
CAPTURE = '''#undef X_G
#define X_G(a) ((void *)(xram_+xpt_[(uint32_t)(a)>>12]+((uint32_t)(a)&4095u)))
#undef X_IMG8
#undef X_IMG16
#undef X_IMG32
#define X_IMG8(a) (*(uint8_t *)(imgb_+(uint32_t)(a)))
#define X_IMG16(a) (*(xu16_u *)(imgb_+(uint32_t)(a)))
#define X_IMG32(a) (*(xu32_u *)(imgb_+(uint32_t)(a)))
'''


def body_of(source, address):
    m = re.search(r'^void f_'+f'{address:08X}'+r'\([^\n]*\)\n\{\n.*?^\}', source, re.M | re.S)
    if not m:
        raise ValueError(f'missing complete function {address:x}')
    return m[0]


def instrument(body):
    site = 0
    lines = []
    for line in body.splitlines(True):
        m = re.search(r'/\* ([0-9A-F]{8}) ', line)
        if m:
            site = int(m[1], 16)
        lines.append(line.replace('X_PREEMPT();', f'do {{ ns_site=0x{site:x}; X_PREEMPT(); }} while(0);'))
    return ''.join(lines)


def generate(a):
    if not __debug__:
        raise RuntimeError('Refusing optimized Python: validation requires assertions')
    image = r.Image(str(a.xbe), str(a.manifest))
    if hashlib.sha256(image.data).hexdigest() != IMAGE or not collision_solver.matches(image):
        raise ValueError('unsupported image or complete solver/caller signature')
    discovery = r.Discovery(image, {}, image.kernel_imports(), lambda *args: None)
    for address in (*ENTRIES, 0x172CB8):
        discovery.add_root(address)
    bodies = {}
    stage = '\n'.join((a.retained/'recomp'/f'code_{i:03d}.c').read_text() for i in (0, 13, 16, 28))
    for address in (*ENTRIES, 0x172CB8):
        fn = discovery.functions[address]
        discovery.lift_function(fn)
        discovery.split_blocks(fn)
        body = r.Emitter(image, discovery, {}, image.kernel_imports(), 'unused', 1,
                         hooks=HaloHooks(image)).emit_function(fn).rstrip()
        if address != 0x172CB8:
            # The disabled historical unlock hook must never enter either lane.
            body = body.replace('\n'.join(collision_solver.ENTRY)+'\n', '')
            if body != body_of(stage, address):
                raise ValueError(f'retained body differs at {address:x}')
        bodies[address] = body
    caller = bodies.pop(0x172CB8)
    inventory = {}
    for address, body in bodies.items():
        calls = set(int(x, 16) for x in re.findall(r'\bf_([0-9A-F]{8})\(c\);', body))
        if not calls <= set(ENTRIES):
            raise ValueError(f'open call closure at {address:x}')
        scopes = re.findall(r'#if defined\(XV_EXPERIMENTAL_OBJECT_JOBS\) && defined\(XV_OBJECT_HOLD_PROFILE\).*?#endif\n', body, re.S)
        if scopes != ([PROFILE] if address == 0x170C10 else []):
            raise ValueError(f'unknown observer scope at {address:x}')
        if any(x in body for x in ('xv_lookup', 'xv_call(', 'XV_HLE_CALL', 'xv_object_solver_')):
            raise ValueError(f'unknown callback at {address:x}')
        inventory[f'{address:08X}'] = dict(calls=sorted(f'{x:08X}' for x in calls),
                                            sha256=hashlib.sha256(body.encode()).hexdigest())
    preamble = '#include "xv_x86rt.h"\nextern unsigned ns_site,ns_fallbacks;\n'+CAPTURE
    declarations = ''.join(f'void f_{x:08X}(xctx *);\n' for x in ENTRIES)
    original = preamble+declarations+'\n'.join(instrument(b) if a.trace else b for b in bodies.values())
    candidate = preamble+declarations+fused(bodies, a)
    if a.inline_primitives:
        # The combined function otherwise crosses GCC's size budget and
        # outlines these tiny, unchanged operations more often than retained
        # generic units. This header is private to the candidate unit.
        header=(ROOT/'recomp/xv_x86rt.h').read_text()
        helpers=('XF_C','XF_O','XF_P','XF_S','XF_Z',
                 'x87_push','x87_pop','x87_load_f32','x87_load_f64','x87_store_f32',
                 'x87_store_f64','x87_compare','x_imul32')
        for helper in helpers:
            pattern=r'static inline ([A-Za-z_][A-Za-z_0-9 *]*\b'+helper+r'\()'
            header,n=re.subn(pattern,r'static inline __attribute__((always_inline)) \1',header)
            if n!=1:raise ValueError(f'unexpected primitive declaration: {helper}')
        hp=a.out/'solver-primitives-private.h';hp.write_text(header)
        candidate=candidate.replace('#include "xv_x86rt.h"',f'#include "{hp}"',1)
    caller_original = caller.replace('f_00172CB8(', 'ns_original_call(')
    caller_candidate = caller.replace('f_00172CB8(', 'ns_candidate_call(').replace(
        'f_00170C10(c);', 'ns_solver_at_172cb8(c,0x172cb8u);')
    callers = '#include "xv_x86rt.h"\n'+CAPTURE+declarations+'void ns_solver_at_172cb8(xctx *,unsigned);\n'+caller_original+'\n'+caller_candidate
    paths = {}
    for name, text in (('reference-private.c', original), ('candidate-private.c', candidate), ('caller-private.c', callers)):
        paths[name] = a.out/name
        paths[name].write_text(text+'\n')
    runtime = (a.retained/'recomp/xv_x86rt.c').read_text()
    movs = runtime[runtime.index('#define STEP(sz)'):runtime.index('\nvoid x_str_stos(')]
    paths['runtime-private.c'] = a.out/'runtime-private.c'
    paths['runtime-private.c'].write_text('#include "xv_x86rt.h"\n#include <stdio.h>\n#include <stdlib.h>\n'
        '#include <psp2/kernel/clib.h>\n#define XV_RT_LOG(...) sceClibPrintf(__VA_ARGS__)\n'+movs+'\n')
    (a.out/'generation.json').write_text(json.dumps(dict(image=IMAGE,closure=inventory,
        frames=a.frames,trace=a.trace,retained=str(a.retained),actor_guard_unchanged=True,
        generic_files_unchanged=True,precision_unchanged=True,default_off=True),indent=2)+'\n')
    return paths


def fused(bodies, a):
    save = ' '.join(f'c->r[{i}]=q{i};' for i in range(8))
    load = ' '.join(f'q{i}=c->r[{i}];' for i in range(8))
    head = '''#ifndef XV_SOLVER_FUSION_PROTOTYPE
#define XV_SOLVER_FUSION_PROTOTYPE 0
#endif
#undef X_R16
#undef X_R8L
#undef X_R8H
#undef X_PUSH32
#undef X_POP32
#undef X_PREEMPT
#define X_R16(i) (*(uint16_t *)&q##i)
#define X_R8L(i) (*(uint8_t *)&q##i)
#define X_R8H(i) (*((uint8_t *)&q##i+1))
#define X_PUSH32(v) do { uint32_t v__=(v);q4-=4;X_M32(q4)=v__; } while(0)
#define X_POP32() ({uint32_t v__=*(xu32_u *)(g_xram+g_xpt[q4>>12]+(q4&4095u));q4+=4;v__;})
'''
    head += f'#define NS_SAVE() do {{ {save} }} while(0)\n#define NS_LOAD() do {{ {load} }} while(0)\n'
    head += '''#define NS_PUBLISH() do { NS_SAVE();memcpy(guest,c,sizeof *c); } while(0)
#define NS_RELOAD() do { memcpy(c,guest,sizeof *c);NS_LOAD(); } while(0)
#define X_PREEMPT() do { if(--c->preempt<=0) { NS_PUBLISH();xv_preempt(guest);NS_RELOAD(); } } while(0)
__attribute__((noinline)) static void ns_solver_fused(xctx *restrict guest,
    uint8_t *xram_, const uint32_t *xpt_, uint8_t *imgb_)
{
    xctx state;memcpy(&state,guest,sizeof state);xctx *c=&state;
'''
    head += '    uint32_t '+','.join(f'q{i}=c->r[{i}]' for i in range(8))+';\n'
    head += '    struct frame { void *resume;uint8_t *arena;const uint32_t *pages;uint8_t *image; };\n'
    head += f'    struct frame frames[{a.frames}];unsigned depth=0;\n    goto NS_ENTRY_00170C10;\n'
    serial = 0
    parts = []
    for address, source in bodies.items():
        body = source[source.index('{')+1:source.rindex('}')]
        body = re.sub(r'    uint8_t \*const (xram_|imgb_) = .*?\n', '', body)
        body = re.sub(r'    uint32_t fk_a = .*?\n', '', body)
        if re.search(r'\bfk_[abr]\b', body):
            raise ValueError(f'live function-local flag cache at {address:x}')
        body = body.replace(PROFILE, '')
        body = re.sub(r'c->r\[([0-7])\]', r'q\1', body)
        def call(m):
            nonlocal serial
            target = int(m[1],16)
            serial += 1
            label = f'NS_CONT_{serial}'
            return f'''if(depth=={a.frames}) {{
        {'ns_fallbacks++;' if a.trace else ''}
        NS_PUBLISH(); f_{target:08X}(guest); NS_RELOAD();
    }} else {{
        frames[depth++]=(struct frame){{&&{label},xram_,xpt_,imgb_}};
        xram_=g_xram;xpt_=g_xpt;imgb_=g_img_base;
        goto NS_ENTRY_{target:08X};
    }}
{label}: ;'''
        body = re.sub(r'\bf_([0-9A-F]{8})\(c\);', call, body)
        # REP MOVS includes the real runtime watch/diagnostic boundary. Keep
        # its original context identity and global mapping semantics.
        body = re.sub(r'x_str_movs\(c, ([^;]+)\);',
                      r'NS_PUBLISH();x_str_movs(guest, \1);NS_RELOAD();', body)
        body = re.sub(r'xv_trap\(c, ([^;]+)\);', r'NS_PUBLISH();xv_trap(guest, \1);NS_RELOAD();',body)
        body = body.replace('return;', 'goto NS_RETURN;')
        if a.trace:
            body = instrument(body)
        parts.append(f'NS_ENTRY_{address:08X}:\n'+body)
    tail = '''
NS_RETURN:
    if(depth) { struct frame frame=frames[--depth];xram_=frame.arena;
        xpt_=frame.pages;imgb_=frame.image;goto *frame.resume; }
    NS_PUBLISH();
}
void ns_solver_at_172cb8(xctx *c,unsigned caller)
{
#if XV_SOLVER_FUSION_PROTOTYPE
    if(caller==0x172cb8u) {
        uint8_t *arena=g_xram,*image=g_img_base;const uint32_t *pages=g_xpt;
'''+PROFILE+'''
        ns_solver_fused(c,arena,pages,image);
        return;
    }
#endif
    f_00170C10(c);
}
'''
    result = head+''.join(parts)+tail
    function = result[result.index('static void ns_solver_fused'):result.index('void ns_solver_at_172cb8')]
    sinks=set(re.findall(r'\b(\w+)\(c\s*[,)]',function))
    allowed={'XF_C','XF_O','XF_P','XF_S','XF_Z','x_shl32','x_shr32','x_sar32',
             'x87_push','x87_pop','x87_load_f32','x87_load_f64','x87_store_f32',
             'x87_store_f64','x87_compare','memcpy','x_imul32'}
    if sinks-allowed:
        raise ValueError(f'unreviewed shadow sinks: {sinks-allowed}')
    if a.negative_control == 'shadow-yield':
        result = result.replace('xv_preempt(guest);','xv_preempt(c);')
    elif a.negative_control == 'stale-yield':
        result = result.replace('xv_preempt(guest);NS_RELOAD();','xv_preempt(guest);NS_LOAD();')
    elif a.negative_control == 'captured-pop':
        result = result.replace('g_xram+g_xpt[q4>>12]', 'xram_+xpt_[q4>>12]')
    return result


def main():
    if not __debug__:
        raise SystemExit('Refusing optimized Python')
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','manifest','retained','out'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--frames',type=int,default=4)
    p.add_argument('--trace',action='store_true')
    p.add_argument('--production-objects',action='store_true')
    p.add_argument('--inline-primitives',action='store_true')
    p.add_argument('--emit-only',action='store_true')
    p.add_argument('--disable',action='store_true')
    p.add_argument('--negative-control',choices=('shadow-yield','stale-yield','captured-pop'))
    p.add_argument('--spec',action='append',help='count,kind,budget,variant,fpscr')
    a=p.parse_args()
    if not 1<=a.frames<=16:p.error('--frames must be 1..16')
    if a.production_objects and a.trace:p.error('retained machine code cannot have source trace injection')
    if a.out.resolve().is_relative_to(ROOT):
        p.error('--out must be outside the source worktree; it contains owned guest code')
    a.out.mkdir(parents=True,exist_ok=False)
    sources=generate(a)
    if not a.emit_only:
        try:
            run_arm(a,sources)
        except Exception as error:
            import traceback
            (a.out/'failure.json').write_text(json.dumps(dict(type=type(error).__name__,
                message=str(error),traceback=traceback.format_exc()),indent=2)+'\n')
            raise


def run_arm(a,sources):
    from test_arm_cluster_runtime import RuntimeMachine,RAM,SIZE,STACK
    from unicorn.arm_const import UC_ARM_REG_SP
    import struct
    cc=os.environ.get('ARM_CC','/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
    flags=['-O2','-g1','-std=gnu11','-fno-strict-aliasing','-ffunction-sections','-fdata-sections',
           '-fstack-usage','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-DTEST_ARM',
           '-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_OBJECT_HOLD_PROFILE',
           f'-DXV_SOLVER_FUSION_PROTOTYPE={0 if a.disable else 1}','-I'+str(ROOT/'recomp')]
    if a.trace:flags+=['-DNS_TRACE']
    src=[sources['candidate-private.c'],sources['caller-private.c'],ROOT/'tools/tests/collision_solver_fusion.c',
         ROOT/'tools/tests/cluster_runtime_arm_imports.c',sources['runtime-private.c']]
    objects=[];commands=[]
    if a.production_objects:
        objects=[a.retained/'build/recomp'/f'code_{i:03d}.o' for i in (0,13,16,28)]
    else:
        src.append(sources['reference-private.c'])
    for i,s in enumerate(src):
        obj=a.out/f'unit-{i}.o';cmd=[cc,*flags,'-c',str(s),'-o',str(obj)]
        subprocess.run(cmd,check=True);commands.append(cmd);objects.append(obj)
    names=['arm_prepare','arm_original','arm_candidate','arm_context_ptr','layout','ns_yields','ns_events',
           'ns_seen','ns_fallbacks','ns_original_pages','ns_alternate_pages','ns_traps','ns_scope_depth']
    elf=a.out/'solver.elf'
    cmd=[cc,*flags,*map(str,objects),'-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,'+
         ','.join('--undefined='+x for x in names),'-lm','-lc','-lgcc','-o',str(elf)]
    if a.production_objects:
        # Retained units have monolithic .text, including unrelated guest
        # functions. Those unresolved targets are outside the proved closure;
        # accidental execution fails on address zero, never succeeds as a stub.
        cmd.insert(-5,'-Wl,--unresolved-symbols=ignore-all')
    if a.trace:cmd.insert(-5,'-Wl,--wrap=x_str_movs')
    subprocess.run(cmd,check=True);commands.append(cmd)
    (a.out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    class StackMachine(RuntimeMachine):
        def step(self,uc,address,size,user):
            self.min_sp=min(self.min_sp,uc.reg_read(UC_ARM_REG_SP));super().step(uc,address,size,user)
        def call(self,*args,**kwargs):
            self.min_sp=0xffffffff;result=super().call(*args,**kwargs)
            result['peak_stack_bytes']=STACK+65024-self.min_sp;return result
    m=StackMachine(elf,profile=True)
    def snap():
        result={'context':bytes(m.uc.mem_read(m.context,m.layout['size'])),
                'memory':bytes(m.uc.mem_read(RAM,SIZE))}
        for name,n in [('ns_yields',4),('ns_events',4),('ns_seen',4),('ns_traps',4),('ns_scope_depth',4),
                       ('ns_original_pages',4096),('ns_alternate_pages',4096),('g_xpt',4)]:
            result[name]=bytes(m.uc.mem_read(m.symbols[name],n))
        return result
    specs=[tuple(int(x,0) for x in s.split(',')) for s in a.spec] if a.spec else [(2,1,100000,0,0)]
    # Initialize the shared runtime's optional watch lookup outside counts.
    m.call('arm_prepare',(2,1,100000));m.call('arm_original')
    rows=[]
    for i,(count,kind,budget,variant,fp) in enumerate(specs):
        packed=kind|(variant<<8)
        m.call('arm_prepare',(count,packed,budget));old=m.call('arm_original',fpscr=fp);expected=snap()
        m.call('arm_prepare',(count,packed,budget));new=m.call('arm_candidate',fpscr=fp);actual=snap()
        checks={k:actual[k]==v for k,v in expected.items()};checks['fpscr']=old['fpscr']==new['fpscr']
        row=dict(count=count,kind=kind,budget=budget,variant=variant,fpscr=fp,original=old,candidate=new,checks=checks,
                 yields=struct.unpack('<I',actual['ns_yields'])[0],seen=hex(struct.unpack('<I',actual['ns_seen'])[0]),
                 return_eax=struct.unpack('<I',actual['context'][:4])[0],
                 fallbacks=struct.unpack('<I',m.uc.mem_read(m.symbols['ns_fallbacks'],4))[0])
        rows.append(row);(a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
        if not all(checks.values()):
            for k,ok in checks.items():
                if not ok and k!='fpscr':
                    print(k,[(j,x,y) for j,(x,y) in enumerate(zip(expected[k],actual[k])) if x!=y][:32],flush=True)
            raise AssertionError(checks)
        print('PASS',i,count,kind,budget,variant,old['instructions'],new['instructions'],old['peak_stack_bytes'],new['peak_stack_bytes'],flush=True)
    if a.production_objects:
        (a.out/'retained-objects.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in objects[:4]},indent=2)+'\n')


if __name__=='__main__':
    main()
