#!/usr/bin/env python3
"""Private full-query continuation prototype. Never changes production hooks.

Owned guest bodies are generated only in --out. All normal-return state stays
strict, including fields proven dead for the prospective 172C95 caller.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925 import collision_traversal as ct

ENTRIES=(0x88110,0x87EA0,0x87E10,0x86F50,0xB0CB0)
IMAGE='4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae'
PROFILE_SCOPE = """#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_HOLD_PROFILE)
    extern unsigned xv_object_hold_children_enabled;
    extern unsigned xv_object_motion_begin(xctx *, unsigned);
    extern void xv_object_motion_end(unsigned *);
    unsigned motion_sample_ __attribute__((cleanup(xv_object_motion_end))) =
        xv_object_hold_children_enabled ? xv_object_motion_begin(c, 6u) : 0;
#endif
"""
PREAMBLE='''#include "xv_x86rt.h"
#ifndef XV_QUERY_FUSION_PROTOTYPE
#define XV_QUERY_FUSION_PROTOTYPE 0
#endif
extern unsigned ct_site;
extern unsigned nq_fallbacks;
extern void ct_observe_entry(xctx *, unsigned);
extern void xv_object_job_stack_probe(xctx *);
void nq_query_at_171f94(xctx *, unsigned);
#undef X_G
#define X_G(a) ((void *)(xram_+xpt_[(uint32_t)(a)>>12]+((uint32_t)(a)&4095u)))
#undef X_IMG8
#undef X_IMG16
#undef X_IMG32
#define X_IMG8(a) (*(uint8_t *)(imgb_+(uint32_t)(a)))
#define X_IMG16(a) (*(xu16_u *)(imgb_+(uint32_t)(a)))
#define X_IMG32(a) (*(xu32_u *)(imgb_+(uint32_t)(a)))
'''

def observe_sites(body):
    site=0; rows=[]
    for line in body.splitlines(True):
        m=re.search(r'/\* ([0-9A-F]{8}) ',line)
        if m:site=int(m[1],16)
        line=line.replace('X_PREEMPT();',f'do {{ ct_site=0x{site:x}; X_PREEMPT(); }} while (0);')
        rows.append(line)
    return ''.join(rows)

def extract_body(path, signature):
    return extract_text(path.read_text(),signature)

def extract_text(source,signature):
    start=source.index('{',source.index(signature))+1
    depth=1;end=start
    while depth:
        if source[end]=='{':depth+=1
        if source[end]=='}':depth-=1
        end+=1
    return source[start:end-1]

def inline_native(body, trace=False):
    """Reuse exact native helper operations without their xctx GPR handoff.

    This copies no owned guest bytes into tracked files: each source helper is
    read and expanded only in the private generated query translation unit.
    """
    cv=extract_body(ROOT/'recomp/kernel/xk_collision_vertices.h','unsigned xv_collision_vertices(')
    if trace:
        for site in (0x87063,0x87094):
            cv=cv.replace('xv_preempt(c);',f'ct_site=0x{site:x}; nq_preempt_marker(c);',1)
        cv=cv.replace('nq_preempt_marker(c);','xv_preempt(c);')
    cv=re.sub(r'    uint32_t a=c->r\[0\].*?;\n','',cv,count=1,flags=re.S)
    cv=re.sub(r'#define CV_(SAVE|LOAD)\(\).*?while\(0\)\n','',cv,flags=re.S)
    cv=cv.replace('#undef CV_SAVE','').replace('#undef CV_LOAD','')
    cv=cv.replace('CV_SAVE();','').replace('CV_LOAD();','')
    cv=cv.replace('xv_preempt(c);','NQ_SAVE(); xv_preempt(c); NQ_LOAD();')
    for old,new in {'a':'q0','x':'q1','d':'q2','b':'q3','sp':'q4','p':'q5','q':'q6','e':'q7','cv_arena':'xram_','cv_pages':'xpt_'}.items():
        cv=re.sub(r'\b'+old+r'\b',new,cv)
    for label in ('vertex','scan','found'):
        cv=re.sub(r'\b'+label+r'\b','NQ_CV_'+label,cv)
    cv=re.sub(r'return ([012]);',r'{ nq_result=\1; goto NQ_CV_DONE; }',cv)
    cv='({ unsigned nq_result=0; '+cv+'\nNQ_CV_DONE: nq_result; })'
    body=body.replace('xv_collision_vertices(c, xram_, xpt_)',cv)

    ss=extract_body(ROOT/'recomp/kernel/xk_segment_sphere.h','xv_segment_sphere(xctx *c,')
    if trace:ss=ss.replace('X_PREEMPT();','do { ct_site=0xb0d99; if(--c->preempt<=0){ NQ_SAVE(); xv_preempt(c); NQ_LOAD(); } } while(0);')
    ss=re.sub(r'return ([012]);',r'{ nq_result=\1; goto NQ_SS_DONE; }',ss)
    ss='({ unsigned nq_result=0; '+ss+'\nNQ_SS_DONE: nq_result; })'
    body=body.replace('xv_segment_sphere(c, xram_, xpt_)',ss)

    distance=extract_body(ROOT/'recomp/kernel/xk_geometry.c','int xv_bsp_sphere_plane_distance(')
    n=0
    while 'xv_bsp_sphere_plane_distance(c)' in body:
        n+=1;label=f'NQ_DISTANCE_DONE_{n}'
        block=re.sub(r'return ([01]);',rf'{{ nq_result=\1; goto {label}; }}',distance)
        block='({ unsigned nq_result=0; '+block+f'\n{label}: nq_result; }})'
        body=body.replace('xv_bsp_sphere_plane_distance(c)',block,1)
    return body

def inline_traversal(body,node2_block):
    """Expand qualified typed regions inside the nonrecursive fused machine.

    This never changes the original production recursive shells. The original
    typed admissions, raw fallbacks and ordered stores are retained verbatim.
    """
    header=ROOT/'recomp/kernel/xk_collision_traversal.h'
    serial=0
    for name in ('xv_ct_node2','xv_ct_node3','xv_ct_project'):
        call=f'{name}(c, xram_, xpt_)'
        template=extract_body(header,name+'(xctx *c,')
        while call in body:
            serial+=1;label=f'NQ_TYPED_{name}_{serial}'
            block=re.sub(r'return ([^;]+);',rf'{{ nq_result=\1; goto {label}; }}',template)
            body=body.replace(call,'({ unsigned nq_result=0; '+block+f'\n{label}: nq_result; }})',1)
    call='xv_ct_node2_block_00087E10(c, xram_, xpt_);'
    while call in body:
        assert node2_block is not None
        serial+=1;label=f'NQ_NODE2_BLOCK_{serial}'
        block=node2_block
        block=re.sub(r'\bL_([0-9A-F]{8})\b',label+r'_L_\1',block)
        block=block.replace('return;',f'goto {label}_DONE;')
        block=inline_traversal(block,None)
        # Recursively expanded expressions have function-scope C labels.
        block=re.sub(r'\bNQ_TYPED_(\w+)\b',label+r'_NQ_TYPED_\1',block)
        body=body.replace(call,'do { '+block+f'\n{label}_DONE: ; }} while(0);',1)
    return body

def fused(bodies, a):
    """Keep GPRs live across all five functions, preserving captured roots.

    Overflow invokes the exact generic child at its original call boundary;
    its guest return word has already been pushed. There is no replay.
    """
    save=' '.join(f'c->r[{i}]=q{i};' for i in range(8))
    load=' '.join(f'q{i}=c->r[{i}];' for i in range(8))
    head='''
#undef X_R16
#undef X_R8L
#undef X_R8H
#undef X_PUSH32
#undef X_POP32
#undef X_PREEMPT
#define X_R16(i) (*(uint16_t *)&q##i)
#define X_R8L(i) (*(uint8_t *)&q##i)
#define X_R8H(i) (*((uint8_t *)&q##i+1))
#define X_PUSH32(v) do { uint32_t v__=(v); q4-=4; X_M32(q4)=v__; } while(0)
/* x_pop32 was defined with GLOBAL roots, unlike emitted integer loads. */
#define X_POP32() ({uint32_t v__=*(xu32_u *)(g_xram+g_xpt[q4>>12]+(q4&4095u));q4+=4;v__;})
'''
    head+=f'#define NQ_SAVE() do {{ {save} }} while(0)\n#define NQ_LOAD() do {{ {load} }} while(0)\n'
    head+='''#define X_PREEMPT() do { if (--c->preempt<=0) { NQ_SAVE(); xv_preempt(c); NQ_LOAD(); } } while(0)
#define NQ_HELPER(expr) ({ NQ_SAVE(); unsigned result__=(expr); NQ_LOAD(); result__; })
__attribute__((noinline)) void query_fused_172c95_171f94(xctx *restrict c)
{
'''
    head+='    '+','.join(f'uint32_t q{i}=c->r[{i}]' if i==0 else f'q{i}=c->r[{i}]' for i in range(8))+';\n'
    head+='''    uint8_t *xram_=g_xram, *imgb_=g_img_base;
    const uint32_t *xpt_=g_xpt;
    struct frame { void *resume; uint8_t *arena; const uint32_t *pages; uint8_t *image; };
'''
    head+=f'    struct frame frames[{a.frames}]; unsigned depth=0;\n'
    head+='    uint32_t fk_a=0,fk_b=0,fk_r=0;(void)fk_a;(void)fk_b;(void)fk_r;\n    goto NQ_ENTRY_00088110;\n'
    parts=[];serial=0
    for address,body in bodies.items():
        node2_block=extract_text(body,'void xv_ct_node2_block_00087E10(') if address==0x87E10 and 'void xv_ct_node2_block_00087E10(' in body else None
        start=body.index('{',body.index(f'void f_{address:08X}'))+1
        body=body[start:body.rindex('}')]
        body=re.sub(r'    uint8_t \*const xram_ = .*?\n','',body)
        body=re.sub(r'    uint8_t \*const imgb_ = .*?\n','',body)
        body=re.sub(r'    uint32_t fk_a = .*?\n','',body)
        # Move the root-only profiler scope to the original-pointer adapter.
        # The shadow must never be presented as a worker's registered context.
        scopes=re.findall(r'#if defined\(XV_EXPERIMENTAL_OBJECT_JOBS\) && defined\(XV_OBJECT_HOLD_PROFILE\).*?#endif\n',body,re.S)
        if scopes!=([PROFILE_SCOPE] if address==0x88110 else []):
            raise ValueError(f'unreviewed profile block at {address:x}')
        body=body.replace(PROFILE_SCOPE,'',1)
        if a.inline_native:
            body=inline_native(body,a.trace)
            body=inline_traversal(body,node2_block)
        body=re.sub(r'c->r\[([0-7])\]',r'q\1',body)
        # Native helpers remain unchanged; their entry/return ABI is explicit.
        for name,args in [('xv_collision_vertices','c, xram_, xpt_'),('xv_segment_sphere','c, xram_, xpt_'),('xv_bsp_sphere_plane_distance','c')]:
            body=body.replace(f'{name}({args})',f'NQ_HELPER({name}({args}))')
        def call(m):
            nonlocal serial
            target=int(m[1],16);assert target in ENTRIES
            serial+=1;label=f'NQ_CONT_{serial}'
            return f'''if(depth=={a.frames}) {{
        {'nq_fallbacks++;' if a.trace else ''}
        NQ_SAVE(); original_{target:08X}(c); NQ_LOAD();
    }} else {{
        frames[depth++]=(struct frame){{&&{label},xram_,xpt_,imgb_}};
        xram_=g_xram; xpt_=g_xpt; imgb_=g_img_base;
        goto NQ_ENTRY_{target:08X};
    }}
{label}: ;'''
        body=re.sub(r'\bf_([0-9A-F]{8})\(c\);',call,body)
        body=body.replace('return;','goto NQ_RETURN;')
        if a.trace:body=observe_sites(body)
        entry=f'NQ_ENTRY_{address:08X}:\n'
        if a.entry_observers:entry+=f'    NQ_SAVE(); ct_observe_entry(c,0x{address:x}); NQ_LOAD();\n'
        parts.append(entry+body)
    tail='''
NQ_RETURN:
    if(depth) {
        struct frame frame=frames[--depth];
        xram_=frame.arena; xpt_=frame.pages; imgb_=frame.image;
        goto *frame.resume;
    }
    NQ_SAVE();
}
/* Selection comes from an actual specialized caller, never a guest stack
 * return-address heuristic. Unsupported routes keep their generic entry. */
void nq_query_at_171f94(xctx *c, unsigned caller)
{
#if XV_QUERY_FUSION_PROTOTYPE
    if(caller==0x172c95u) {
#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_HOLD_PROFILE)
        extern unsigned xv_object_hold_children_enabled;
        extern unsigned xv_object_motion_begin(xctx *,unsigned);
        extern void xv_object_motion_end(unsigned *);
        unsigned motion_sample_ __attribute__((cleanup(xv_object_motion_end))) =
            xv_object_hold_children_enabled ? xv_object_motion_begin(c,6u) : 0;
#endif
        query_fused_172c95_171f94(c);
    } else original_00088110(c);
#else
    (void)caller;original_00088110(c);
#endif
}
void candidate_00088110(xctx *c) { nq_query_at_171f94(c,0x172c95u); }
void candidate_00087EA0(xctx *c) { original_00087EA0(c); }
void candidate_00087E10(xctx *c) { original_00087E10(c); }
'''
    result=head+''.join(parts)+tail
    if a.generic_fallback:
        result=result.replace('nq_query_at_171f94(c,0x172c95u);','nq_query_at_171f94(c,0u);')
    if a.shadow_context:
        assert a.inline_native, 'shadow context needs all native preempt sites in the fused body'
        result=result.replace('void query_fused_172c95_171f94(xctx *restrict c)\n{',
          'void query_fused_172c95_171f94(xctx *restrict guest)\n{\n'
          '    xctx state; memcpy(&state,guest,sizeof state); xctx *c=&state;')
        result=result.replace('xv_preempt(c);','memcpy(guest,c,sizeof *c); xv_preempt(guest); memcpy(c,guest,sizeof *c);')
        result=re.sub(r'ct_observe_entry\(c,([^)]*)\);',
          r'memcpy(guest,c,sizeof *c); ct_observe_entry(guest,\1); memcpy(c,guest,sizeof *c);',result)
        result=re.sub(r'original_([0-9A-F]{8})\(c\); NQ_LOAD\(\);',
          r'memcpy(guest,c,sizeof *c); original_\1(guest); memcpy(c,guest,sizeof *c); NQ_LOAD();',result)
        result=result.replace('    NQ_SAVE();\n}\n','    NQ_SAVE(); memcpy(guest,c,sizeof *c);\n}\n')
    if a.negative_control=='stale-after-yield':
        assert a.shadow_context
        old='xv_preempt(guest); memcpy(c,guest,sizeof *c);'
        assert old in result
        result=result.replace(old,'xv_preempt(guest); /* intentionally retain stale shadow */')
    if a.negative_control=='captured-root-pop':
        old='g_xram+g_xpt[q4>>12]+(q4&4095u)'
        assert result.count(old)==1
        result=result.replace(old,'xram_+xpt_[q4>>12]+(q4&4095u)')
    if a.negative_control=='shadow-observer':
        assert a.shadow_context
        result=result.replace('xv_preempt(guest);','xv_preempt(c);')
    if a.shadow_context and not a.negative_control:
        # Only audited field-local inline helpers may receive the shadow.
        # Real callbacks and original-child fallback receive guest identity.
        function=result[result.index('void query_fused_'):result.index('void nq_query_at_')]
        sinks=set(re.findall(r'\b(\w+)\(c\s*[,)]',function))
        allowed={'XF_C','XF_O','XF_P','XF_S','XF_Z','x_shl32','x_shr32','x_sar32',
          'x87_push','x87_pop','x87_load_f32','x87_store_f32','x87_compare','x_shufps','xv_ss_flags','memcpy'}
        assert sinks<=allowed, 'unreviewed shadow context sink: '+str(sinks-allowed)
    return result

def generate(a):
    if not __debug__:
        raise RuntimeError("Refusing optimized Python: generation safety checks require assertions.")
    image=r.Image(str(a.xbe),str(a.manifest))
    assert hashlib.sha256(image.data).hexdigest()==IMAGE
    d=r.Discovery(image,{},image.kernel_imports(),lambda *args:None)
    for x in ENTRIES:d.add_root(x)
    for x in ENTRIES:d.lift_function(d.functions[x]);d.split_blocks(d.functions[x])
    bodies={x:r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=HaloHooks(image)).emit_function(d.functions[x]) for x in ENTRIES}
    # The only scope moved out of the fused closure is root site 6. Fail
    # closed if a future production hook adds another context observer.
    scope_inventory={hex(address):[int(site) for site in re.findall(r'xv_object_motion_begin\(c, (\d+)u\)',body)]
                     for address,body in bodies.items()}
    expected={hex(address):([6] if address==0x88110 else []) for address in ENTRIES}
    if scope_inventory!=expected:
        raise ValueError('unreviewed motion scope inventory: '+str(scope_inventory))
    candidates=bodies
    if a.candidate_traversal=='baseline':
        typed_hook=ct.hook
        try:
            ct.hook=lambda address,body:body
            candidates={x:r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=HaloHooks(image)).emit_function(d.functions[x]) for x in ENTRIES}
        finally:ct.hook=typed_hook
    text=PREAMBLE+''.join(f'void original_{x:08X}(xctx *);\n' for x in ENTRIES)
    for address,body in bodies.items():
        (a.out/f'original-{address:08X}-private.c').write_text(body)
        body=re.sub(r'\bf_([0-9A-F]{8})\b',r'original_\1',body)
        if a.entry_observers:
            marker='    uint8_t *const imgb_ = g_img_base; (void)imgb_;\n'
            assert body.count(marker)==1
            if address==0x88110:
                marker=re.search(r'#if defined\(XV_EXPERIMENTAL_OBJECT_JOBS\) && defined\(XV_OBJECT_HOLD_PROFILE\).*?#endif\n',body,re.S).group(0)
            body=body.replace(marker,marker+f'    ct_observe_entry(c,0x{address:x});\n')
        text+=(observe_sites(body) if a.trace else body)+'\n'
    if a.callers:text+=caller_bodies(image,d,a)
    text+=fused(candidates,a)
    if a.trace:
        for filename in ('xk_collision_vertices.h','xk_segment_sphere.h'):
            header=(ROOT/'recomp/kernel'/filename).read_text()
            header=header.replace('#include "../xv_x86rt.h"','#include "xv_x86rt.h"')
            if filename=='xk_collision_vertices.h':
                for site in (0x87063,0x87094):
                    header=header.replace('xv_preempt(c);',f'ct_site=0x{site:x}; nq_preempt_marker(c);',1)
                header=header.replace('nq_preempt_marker(c);','xv_preempt(c);')
            else:header=header.replace('X_PREEMPT();','do { ct_site=0xb0d99; X_PREEMPT(); } while(0);')
            path=a.out/filename;path.write_text(header)
            text=text.replace(f'"kernel/{filename}"',f'"{path}"')
    p=a.out/'query-private.c';p.write_text(text)
    (a.out/'oracle.json').write_text(json.dumps({'owned_image_sha256':IMAGE,'entries':[hex(x) for x in ENTRIES],
      'options':{k:str(v) if isinstance(v,Path) else v for k,v in vars(a).items()},
      'frames':a.frames,'scope_inventory':scope_inventory,'strict_normal_return':True,'generic_unchanged':True,'trace':a.trace,
      'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__),ROOT/'tools/tests/collision_query_fusion.c']},
      'body_sha256':{hex(x):hashlib.sha256(s.encode()).hexdigest() for x,s in bodies.items()}},indent=2)+'\n')
    if a.retained_dir:integration_sources(a,bodies,candidates)
    return p

def integration_sources(a,bodies,candidates):
    """Private default-OFF separate-unit patch; never edit the build tree.

    One real CALL 172C95 selects a cloned 171F10, retaining every other call,
    hook and interior generic entry. Overflow always calls generic functions.
    """
    if not (a.emit_only and a.inline_native and a.shadow_context) or a.trace or a.entry_observers:
        raise ValueError('--retained-dir requires uninstrumented --emit-only --inline-native --shadow-context')
    units={i:(a.retained_dir/f'code_{i:03d}.c').read_text() for i in (13,16,28)}
    for address,body in bodies.items():
        unit=units[16 if address==0xB0CB0 else 13]
        signature=f'void f_{address:08X}('
        if extract_text(unit,signature)!=extract_text(body,signature):
            raise ValueError(f'retained function differs from qualified oracle: {address:x}')
    for address in (0x171F10,0x172BF0):
        # Owned generated caller files were emitted with all current hooks.
        body=(a.out/f'caller_original-{address:08X}-private.c').read_text()
        if extract_text(units[28],f'void f_{address:08X}(')!=extract_text(body,f'void f_{address:08X}('):
            raise ValueError(f'retained caller differs from qualified oracle: {address:x}')
    guard='#ifndef XV_QUERY_FUSION_PROTOTYPE\n#define XV_QUERY_FUSION_PROTOTYPE 0\n#endif\n'
    code=fused(candidates,a).split('void candidate_00088110(')[0]
    code=re.sub(r'\boriginal_([0-9A-F]{8})\b',r'f_\1',code)
    # Keep this out of the very large code_013 TU: its inlining budget turns
    # otherwise useful fusion into a measured regression. Generic objects do
    # not need rebuilding; only the caller unit and this generated unit change.
    includes='\n'.join('#include "'+name+'"' for name in
        ('xv_recomp_protos.h','kernel/xk_collision_vertices.h',
         'kernel/xk_segment_sphere.h','kernel/xk_collision_traversal.h'))+'\n'
    capture_macros=PREAMBLE[PREAMBLE.index('#undef X_G'):]
    query_unit=guard+'#if XV_QUERY_FUSION_PROTOTYPE\n'+includes+capture_macros+code+'\n#endif\n'
    generic=extract_text(units[28],'void f_00171F10(')
    if generic.count('f_00088110(c);')!=1:
        raise ValueError('unexpected BSP query call inventory')
    specialized=generic.replace('f_00088110(c);','nq_query_at_171f94(c,0x172c95u);')
    caller=extract_text(units[28],'void f_00172BF0(')
    call='    /* 00172C95  call 00171F10h */\n    X_PUSH32(0x172C9Au);\n    f_00171F10(c);'
    if caller.count(call)!=1:
        raise ValueError('unexpected specialized caller instruction sequence')
    selected=caller.replace(call,call.replace('    f_00171F10(c);',
      '#if XV_QUERY_FUSION_PROTOTYPE\n    nq_collection_172c95(c);\n#else\n    f_00171F10(c);\n#endif'))
    prefix=guard+'#if XV_QUERY_FUSION_PROTOTYPE\n#include "xv_recomp_protos.h"\nvoid nq_query_at_171f94(xctx *,unsigned);\nstatic void nq_collection_172c95(xctx *);\n#endif\n'
    out28=prefix+units[28].replace(caller,selected,1)+'\n#if XV_QUERY_FUSION_PROTOTYPE\nstatic void nq_collection_172c95(xctx *restrict c)\n{'+specialized+'}\n#endif\n'
    directory=a.out/'integration';directory.mkdir()
    (directory/'code_028.c').write_text(out28)
    (directory/'query_fusion.c').write_text(query_unit)
    import difflib
    patch=''.join(difflib.unified_diff(units[28].splitlines(True),out28.splitlines(True),
         fromfile='a/recomp/code_028.c',tofile='b/recomp/code_028.c'))
    patch+=''.join(difflib.unified_diff([],query_unit.splitlines(True),
         fromfile='/dev/null',tofile='b/recomp/query_fusion.c'))
    (directory/'owned-private.patch').write_text(patch)
    (directory/'contract.json').write_text(json.dumps({
      'default':0,'changed_call_pc':'0x172c95','specialized_parent':'0x171f10',
      'specialized_query_pc':'0x171f94','generic_bodies_retained':True,
      'motion_scopes':['172bf0:2','171f10:3','88110:6'],
      'collection_hook_retained':'0x172034','deployment_admitted':False,
      'separate_translation_unit':True,'unchanged_objects':['code_013.o','code_016.o'],
      'build_requirement':'Add generated query_fusion.c as a separate unit; apply the same tracked 0/1 feature flag to it and code_028.c.',
      'limitations':['No native stack bound admission is added.','No hardware cost or end-to-end FPS is measured.'],
      'retained_sha256':{str(i):hashlib.sha256(t.encode()).hexdigest() for i,t in units.items()}
    },indent=2)+'\n')

def caller_bodies(image,d,a):
    # Real guest call chain: full 172BF0, or its CALL 172C95 with prepared
    # arguments. We stop at the first out-of-scope callback, recording its full
    # entry state. Both lanes execute the identical original continuations.
    addresses=((0x172BF0 if a.full_caller else 0x172C95),0x171F10,0x1D130)
    for x in addresses:d.add_root(x)
    for x in addresses:d.lift_function(d.functions[x]);d.split_blocks(d.functions[x])
    text='extern unsigned ct_boundary;\nvoid candidate_00088110(xctx *);\n'
    for prefix in ('caller_original','caller_candidate'):
        text+=''.join(f'void {prefix}_{x:08X}(xctx *);\n' for x in addresses)
        for x in addresses:
            body=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=HaloHooks(image)).emit_function(d.functions[x])
            (a.out/f'{prefix}-{x:08X}-private.c').write_text(body)
            body=body.replace(f'void f_{x:08X}(',f'void {prefix}_{x:08X}(')
            def call(m):
                target=int(m[1],16)
                if target in addresses:
                    return f'{prefix}_{target:08X}(c); if(ct_boundary)return;'
                if target==0x88110:
                    lane='original' if prefix=='caller_original' else 'candidate'
                    return f'{lane}_00088110(c);'
                return f'ct_boundary=0x{target:x}; ct_observe_entry(c,ct_boundary); return;'
            body=re.sub(r'\bf_([0-9A-F]{8})\(c\);',call,body)
            text+=(observe_sites(body) if a.trace else body)+'\n'
    return text

def main():
    if not __debug__:
        raise SystemExit("Refusing optimized Python: generation safety checks require assertions.")
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('xbe','manifest','out'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--frames',type=int,default=32)
    p.add_argument('--trace',action='store_true')
    p.add_argument('--entry-observers',action='store_true')
    p.add_argument('--inline-native',action='store_true')
    p.add_argument('--shadow-context',action='store_true')
    p.add_argument('--callers',action='store_true')
    p.add_argument('--full-caller',action='store_true',help='Execute the complete retained 172BF0 caller with its profile scope')
    p.add_argument('--suite',choices=('cost','observations','callers'),default='cost')
    p.add_argument('--generic-fallback',action='store_true')
    p.add_argument('--disable-fusion',action='store_true')
    p.add_argument('--hold-profile-mode',type=int,choices=(0,1))
    p.add_argument('--candidate-traversal',choices=('typed','baseline'),default='typed')
    p.add_argument('--typed-default',type=int,choices=(0,1),default=1)
    p.add_argument('--negative-control',choices=('stale-after-yield','captured-root-pop','shadow-observer'))
    p.add_argument('--emit-only',action='store_true')
    p.add_argument('--retained-dir',type=Path,help='Emit a private default-OFF patch against retained code_013/016/028 units')
    p.add_argument('--arm',action='store_true')
    p.add_argument('--cases',type=int,default=64)
    p.add_argument('--case-start',type=int,default=0)
    p.add_argument('--sanitize',action='store_true')
    a=p.parse_args()
    if a.retained_dir:a.full_caller=True
    if a.full_caller:a.callers=True
    if a.candidate_traversal=='typed' and not a.inline_native:
        p.error('--candidate-traversal typed requires --inline-native')
    if not 1 <= a.frames <= 256:
        p.error("--frames must be between 1 and 256")
    a.out.mkdir(parents=True,exist_ok=False)
    source=generate(a)
    if a.emit_only:return
    flags=['-O2','-g1','-std=gnu11','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-fstack-usage',
      f'-DXV_QUERY_FUSION_PROTOTYPE={0 if a.disable_fusion else 1}',
      '-DXV_NATIVE_COLLISION_VERTICES','-DXV_NATIVE_COLLISION_VERTICES_DEFAULT=1',
      '-DXV_NATIVE_SEGMENT_SPHERE','-DXV_NATIVE_SEGMENT_SPHERE_DEFAULT=1','-DXV_NATIVE_BSP_SPHERE',
      '-DXV_NATIVE_COLLISION_TRAVERSAL',f'-DXV_NATIVE_COLLISION_TRAVERSAL_DEFAULT={a.typed_default}',
      '-I'+str(ROOT/'recomp'),f'-DCT_CASES={a.cases}']
    if a.callers:
        flags+=['-DCT_CALLERS','-DXV_NATIVE_OBJECT_COLLECT','-DXV_NATIVE_OBJECT_COLLECT_DEFAULT=1']
    if a.full_caller:flags+=['-DCT_FULL_CALLER']
    if a.hold_profile_mode is not None:
        flags+=['-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_OBJECT_HOLD_PROFILE',f'-DCT_HOLD_ENABLED={a.hold_profile_mode}']
    if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
    sources=[source,ROOT/'tools/tests/collision_query_fusion.c']+[ROOT/'recomp/kernel'/n for n in
       ('xk_collision_vertices_control.c','xk_segment_sphere_control.c','xk_geometry.c','xk_collision_traversal_control.c')]
    if a.callers:sources.append(ROOT/'recomp/kernel/xk_object_collect.c')
    if a.arm:return arm(a,flags,sources)
    cmd=[os.environ.get('CC','cc'),*flags,*map(str,sources),'-Wl,--gc-sections,--wrap=xv_preempt,--wrap=xv_object_collect_refs','-lm','-pthread','-o',str(a.out/'test')]
    (a.out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n');subprocess.run(cmd,check=True)
    run=subprocess.run([str(a.out/'test')],capture_output=True,text=True)
    (a.out/'run.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode()

def arm(a,flags,sources):
    import struct
    from test_arm_cluster_runtime import RuntimeMachine,RAM,SIZE
    from unicorn.arm_const import UC_ARM_REG_SP
    cc=os.environ.get('ARM_CC','/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
    flags+=['-DTEST_ARM','-mthumb','-mcpu=cortex-a9','-mfpu=neon']
    sources+=[ROOT/'tools/tests/cluster_runtime_arm_imports.c']
    objects=[];commands=[]
    for i,source in enumerate(sources):
        obj=a.out/f'{i}.o';cmd=[cc,*flags,'-c',str(source),'-o',str(obj)]
        subprocess.run(cmd,check=True);commands.append(cmd);objects.append(str(obj))
    names=['arm_prepare','arm_original','arm_candidate','arm_context_ptr','layout','ct_yields','ct_events','ct_seen','ct_boundary','ct_collect_calls','nq_fallbacks','ct_original_pages','ct_alternate_pages','g_img_base']
    elf=a.out/'query.elf';cmd=[cc,*flags,*objects,'-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,--wrap=xv_object_collect_refs,'+','.join('--undefined='+n for n in names),'-lc','-lgcc','-o',str(elf)]
    subprocess.run(cmd,check=True);commands.append(cmd)
    (a.out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    class StackMachine(RuntimeMachine):
        def step(self,uc,address,size,user):
            self.min_sp=min(self.min_sp,uc.reg_read(UC_ARM_REG_SP))
            super().step(uc,address,size,user)
        def call(self,*args,**kwargs):
            self.min_sp=0xffffffff
            result=super().call(*args,**kwargs)
            from test_arm_cluster_runtime import STACK
            result['peak_stack_bytes']=STACK+65024-self.min_sp
            return result
    m=StackMachine(elf,profile=True);m.imports={k:v for k,v in m.imports.items() if v!='__wrap_xv_preempt'}
    def snapshot():
        return {'context':bytes(m.uc.mem_read(m.context,m.layout['size'])),'memory':bytes(m.uc.mem_read(RAM,SIZE)),
          **{name:bytes(m.uc.mem_read(m.symbols[name],size)) for name,size in [('ct_yields',4),('ct_events',4),('ct_seen',4),('ct_boundary',4),('ct_collect_calls',4),('ct_original_pages',4096),('ct_alternate_pages',4096),('g_xpt',4)]}}
    specs=[(d,v,100000,0) for d in (1,3,16,64,128) for v in (0,3,4,8,12,15,1<<25)
           if d<=16 or v!=(1<<25)]
    if a.suite=='observations':
        specs=[(3,(1<<23)|12,1,0),(3,(1<<22)|12,1,0)]
        specs += [(3,(mutation<<14)|v,1,0) for mutation in range(8) for v in (0,4,8,15)]
        specs += [(3,(profile<<28)|(1<<24)|(top<<8),100000,fp) for profile in (1,3,5,9,11,12,13,14,15)
              for top,fp in ((0,0),(1,0x400000),(3,0x800000),(7,0xc00000),(5,0x0300009f))]
    if a.suite=='callers':
        assert a.callers
        specs=[(3,v|(mode<<20),budget,fp) for v in (0,3,4,8,12,(1<<23)|12,(1<<22)|12)
               for mode in range(4) for budget,fp in ((100000,0),(1,0),(100000,0x0300009f))]
    if a.negative_control:
        specs=[(16,(5<<14)|4,1,0)] if a.negative_control=='captured-root-pop' else [(3,(7<<14)|8,1,0)]
    rows=[]
    for i,(depth,variant,budget,fp) in enumerate(specs[a.case_start:a.case_start+a.cases],a.case_start):
        m.call('arm_prepare',(depth,variant,budget));original=m.call('arm_original',fpscr=fp);expected=snapshot()
        m.call('arm_prepare',(depth,variant,budget));candidate=m.call('arm_candidate',fpscr=fp);actual=snapshot()
        checks={k:actual[k]==v for k,v in expected.items()};checks['fpscr']=original['fpscr']==candidate['fpscr']
        row={'depth':depth,'variant':variant,'budget':budget,'fpscr':fp,'original':original,'candidate':candidate,'checks':checks,
             'yields':struct.unpack('<I',actual['ct_yields'])[0],'seen':struct.unpack('<I',actual['ct_seen'])[0],
             'boundary':hex(struct.unpack('<I',actual['ct_boundary'])[0]),
             'collection_calls':struct.unpack('<I',actual['ct_collect_calls'])[0],
             'fallbacks':struct.unpack('<I',m.uc.mem_read(m.symbols['nq_fallbacks'],4))[0]}
        rows.append(row);(a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
        if not all(checks.values()):
            for k,good in checks.items():
                if not good and k!='fpscr':print(k,[(i,x,y) for i,(x,y) in enumerate(zip(expected[k],actual[k])) if x!=y][:32],flush=True)
            raise AssertionError(checks)
        print('PASS ARM',i,depth,variant,budget,fp,original['instructions'],candidate['instructions'],original['peak_stack_bytes'],candidate['peak_stack_bytes'],flush=True)

if __name__=='__main__':main()
