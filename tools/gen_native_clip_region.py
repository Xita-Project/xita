#!/usr/bin/env python3
"""Generate full-state locals for the audited positive clip-wrapper region.

Owned inputs/outputs stay local. Original setup and first signed tests execute
before admission; original memory accesses, scratch writes, guards and scheduler
handoffs stay in order. No polygon prepass or relaxed guest ABI is used.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from recompiler import xita_recomp as r
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925.clip_region import IMAGE_SHA256, SPANS, hook
from tools.gen_native_clip import ARM_OPERAND_ORDER, ordered_arm_fp_body
from games.halo_ce_3925.discovery import HaloDiscovery
from tools.clip_distance_spans import transform as distance_spans
FIELDS=('f_kind','f_op1','f_op2','f_res','f_bits','f_cf_override','f_cf','f_of_override','f_of','fsw')


def generate(xbe=None,manifest=None):
    if not __debug__: raise RuntimeError('generation requires assertions')
    img=r.Image(str(xbe or ROOT/'haloce/default.xbe'),str(manifest or ROOT/'local/halo_ce_3925/game_manifest.json'))
    assert hashlib.sha256(img.data).hexdigest()==IMAGE_SHA256,'unsupported image'
    depths,instructions={},{}
    for address,(size,digest) in SPANS.items():
        code=img.bytes_at(address,size)
        assert code is not None and len(code)==size and hashlib.sha256(code).hexdigest()==digest,'unsupported region span'
        instructions.update({i.ip:i for i in r.Decoder(32,code,ip=address)})
        todo=[(address,0)]
        while todo:
            pc,d=todo.pop()
            if pc in depths:
                assert depths[pc]==d,'unbalanced x87 join'
                continue
            depths[pc]=d;i=instructions[pc];mn=r.MN[i.mnemonic]
            delta={'fld':-1,'fstp':1,'faddp':1,'fdivp':1,'fcomp':1,'fcompp':2}
            assert not mn.startswith('f') or mn in set(delta)|{'fdiv','fsqrt','fst','fabs','fadd','fchs','fcom','fmul','fnstsw','fsub','fsubr','fxch'},mn
            d+=delta.get(mn,0)
            assert -7<=d<=0
            if i.flow_control==r.FlowControl.RETURN: assert d==0
            elif i.flow_control==r.FlowControl.UNCONDITIONAL_BRANCH: todo.append((i.near_branch_target,d))
            elif i.flow_control==r.FlowControl.CONDITIONAL_BRANCH: todo.extend(((i.next_ip,d),(i.near_branch_target,d)))
            else:
                assert i.flow_control in (r.FlowControl.NEXT,r.FlowControl.CALL)
                if i.flow_control==r.FlowControl.CALL: assert d==0 and i.near_branch_target in SPANS
                todo.append((i.next_ip,d))
    assert set(instructions)-set(depths)=={0xB7F4D,0xB7269}
    disc=HaloDiscovery(img,{},img.kernel_imports(),lambda *args:None)
    for a in SPANS:
        disc.add_root(a);disc.lift_function(disc.functions[a]);disc.split_blocks(disc.functions[a])
    emit=r.Emitter(img,disc,{},img.kernel_imports(),'unused',1,hooks=NoGameHooks())
    raw={a:emit.emit_function(disc.functions[a]) for a in SPANS}
    # Also assert the tracked admission accepts both ordinary and diagnostic
    # emission. The arithmetic reference is independently emitted without hooks.
    hook(raw[0xB7F10])
    emit.trace_funcs=True
    traced={a:emit.emit_function(disc.functions[a]) for a in SPANS}
    hook(traced[0xB7F10])
    body=[];sites={0xB71F1:0,0xB7213:0}
    for address in (0xB7F10,0x117B0,0xB71C0):
        lines=raw[address].splitlines();lines=lines[lines.index(f'L_{address:08X}:'):-1]
        if address==0xB7F10: lines=['    goto L_000B7F55;']+lines[lines.index('L_000B7F34:'):]
        depth=0;pc=None
        for line in lines:
            match=re.search(r'/\* ([0-9A-F]{8}) ',line)
            if match: pc=int(match[1],16);depth=depths[pc]
            if line.strip()=='f_000117B0(c);':
                body.extend(['    work->planes++; F_MARK(0x117B0); goto L_000117B0;', 'F_AFTER_PLANE:', '    F_MARK(0xB7F10);']);continue
            if line.strip()=='f_000B71C0(c);':
                body.extend(['    F_SAVE(0); F_MARK(0xB71C0);',
                             '    lock=F_LOCK(); xv_clip_region_account(); work->clips++;',
                             '    saved_fn=F_TRACE_ALLOWED(c) && &xv_cur_fn ? xv_cur_fn : 0;',
                             '    fp=c->fsp; F_LOAD(); goto L_000B71C0;', 'F_AFTER_CLIP:',
                             '    if ((uint16_t)r0==0xffffu) work->capacity_failures++;',
                             '    F_MARK(0xB7F10);']);continue
            if line.strip()=='f_0001D130(c);':
                assert address==0xB71C0
                body.extend([f'    F_SAVE({depth&7}); f_0001D130(c);',
                             '    if (F_TRACE_ALLOWED(c) && &xv_cur_fn) {',
                             '        if (&xv_watch_n && xv_watch_n && xv_watch_leave) xv_watch_leave(xv_cur_fn,saved_fn,c);',
                             '        xv_cur_fn=saved_fn;', '    }',
                             f'    fp=(c->fsp-{depth&7})&7; F_LOAD();']);continue
            if 'x_str_movs(c,' in line:
                body.extend([f'    F_SAVE({depth&7});',line,f'    fp=(c->fsp-{depth&7})&7; F_LOAD();']);continue
            if 'x87_compare(c,' in line:
                body.append(f'    flags.fsp=(fp+{depth&7})&7;');line=line.replace('x87_compare(c,','x87_compare(&flags,')
            line=re.sub(r'X_ST\((\d)\)',lambda m:f's{(depth+int(m[1]))&7}',line)
            match=re.fullmatch(r'    x87_push\(c, (.*)\);',line)
            if match: depth-=1;line=f'    s{depth&7}={match[1]};'
            elif line.strip()=='x87_pop(c);': depth+=1;continue
            line=re.sub(r'c->r\[([0-7])\]',r'r\1',line)
            for f in FIELDS: line=line.replace('c->'+f,'flags.'+f)
            for macro in ('R16','R8L','R8H','PUSH32','POP32','FLAGS'): line=line.replace('X_'+macro+'(','F_'+macro+'(')
            for helper in ('XF_Z','XF_S','XF_O','XF_C','XF_P','x_shl32','x_shr32'): line=line.replace(helper+'(c',helper+'(&flags')
            line=line.replace('X_PREEMPT()',f'F_PREEMPT({depth&7})')
            if 'return;' in line:
                if address==0xB7F10: line=line.replace('return;',f'F_SAVE({depth&7}); return;')
                elif address==0x117B0: line=line.replace('return;','goto F_AFTER_PLANE;')
                else: line=line.replace('return;',f'F_SAVE({depth&7}); F_UNLOCK(); fp=c->fsp; F_LOAD(); goto F_AFTER_CLIP;')
            body.append(line)
            if pc in sites and line.startswith('    r1 = '):
                sites[pc]+=1
                body.append('    if ((int16_t)r1>0) work->input_vertices+=(uint16_t)r1;')
    assert sites=={0xB71F1:1,0xB7213:2}
    body=ordered_arm_fp_body(body)
    text='\n'.join(body)
    for pc in (0x117CC,0x1181D):
        text,n=re.subn(r'(/\* '+f'{pc:08X}'+r'  faddp \*/\n)    (s\d) = (s\d) \+ (s\d);',
                     lambda m:m[1]+f'    {m[2]} = clip_ordered_add({m[3]}, {m[4]});',text)
        assert n==1
    # New context-taking helpers require a fresh boundary audit.
    assert set(re.findall(r'\b(\w+)\(c(?:,|\))',text)) <= {'F_TRACE_ALLOWED','f_0001D130','x_str_movs','x87_load_f64','x87_load_f32','x87_store_f32'}, set(re.findall(r'\b(\w+)\(c(?:,|\))',text))
    load=' '.join([*(f'r{i}=c->r[{i}];' for i in range(8)),*(f's{i}=c->st[(fp+{i})&7];' for i in range(8)),*(f'flags.{f}=c->{f};' for f in FIELDS)])
    save=' '.join([*(f'c->r[{i}]=r{i};' for i in range(8)),*(f'c->st[(fp+{i})&7]=s{i};' for i in range(8)),*(f'c->{f}=flags.{f};' for f in FIELDS)])
    header=f'''/* Generated from the owned image by tools/gen_native_clip_region.py.
 * Image SHA256 {IMAGE_SHA256}; never distribute generated game code.
 * Full state and mapped aliases; original per-clip guard, stack and preemption.
 */
#include "xv_x86rt.h"
#include "kernel/xk_object_jobs.h"
#include "kernel/xk_clip_region.h"
extern void f_0001D130(xctx *);
extern volatile uint32_t xv_cur_fn __attribute__((weak));
extern int xv_watch_n __attribute__((weak)), xv_trace_funcs __attribute__((weak));
extern void xv_watch_leave(uint32_t,uint32_t,xctx *) __attribute__((weak));
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
#define F_TRACE_ALLOWED(c) (!xv_is_object_job(c))
#define F_LOCK() xv_object_math_lock()
#define F_UNLOCK() xv_object_math_unlock(&lock)
#else
#define F_TRACE_ALLOWED(c) 1
#define F_LOCK() 0
#define F_UNLOCK() ((void)0)
#endif
#define F_MARK(a) do {{ if (markers && &xv_cur_fn) xv_cur_fn=(a); }} while(0)
#define F_LOAD() do {{ {load} }} while(0)
#define F_SAVE(d) do {{ {save} c->fsp=(fp+(d))&7; }} while(0)
#define F_PREEMPT(d) do {{ if(--c->preempt<=0){{F_SAVE(d);xv_preempt(c);fp=(c->fsp-(d))&7;F_LOAD();}} }}while(0)
#define F_R16(i) (*(uint16_t*)&r##i)
#define F_R8L(i) (*(uint8_t*)&r##i)
#define F_R8H(i) (*((uint8_t*)&r##i+1))
#define F_PUSH32(v) do {{uint32_t v__=(uint32_t)(v);r4-=4;X_M32(r4)=v__;}}while(0)
#define F_POP32() ({{uint32_t v__=X_M32(r4);r4+=4;v__;}})
#define F_FLAGS(kind,a,b,res,bits) do {{flags.f_kind=(kind);flags.f_op1=(uint32_t)(a);flags.f_op2=(uint32_t)(b);flags.f_res=(uint32_t)(res);flags.f_bits=(bits);flags.f_cf_override=flags.f_of_override=0;}}while(0)
static void __attribute__((noinline)) clip_region_native(xctx *restrict c,int markers,xv_clip_region_work *work){{
unsigned fp=c->fsp; xctx flags; int lock=0; uint32_t saved_fn=0;
uint32_t {','.join('r'+str(i) for i in range(8))};
double {','.join('s'+str(i) for i in range(8))};
uint8_t *const xram_=g_xram,*const imgb_=g_img_base;const uint32_t*const xpt_=g_xpt;(void)xram_;(void)imgb_;(void)xpt_;
uint32_t fk_a=0,fk_b=0,fk_r=0;(void)fk_a;(void)fk_b;(void)fk_r;
F_LOAD();
'''
    footer='''
}
int xv_math_clip_region(xctx *c,int markers) {
    /* Diagnostic callbacks can change guest state at every function edge.
     * Keep the exact original calls when those modes are enabled. */
    if ((&xv_watch_n && xv_watch_n) || (&xv_trace_funcs && xv_trace_funcs)) return 0;
    if (!xv_clip_region_begin()) return 0;
    xv_clip_region_work work={.regions=1};
    clip_region_native(c,markers,&work);
    xv_clip_region_end(&work);
    return 1;
}
'''
    return distance_spans(ARM_OPERAND_ORDER+header+text+footer),raw,traced


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--xbe');p.add_argument('--manifest');p.add_argument('--check',action='store_true');a=p.parse_args()
    source,raw,traced=generate(a.xbe,a.manifest)
    outputs={ROOT/'recomp/kernel/xk_clip_region.c':source,
             ROOT/'recomp/host/build/clip_region_original.json':json.dumps({'raw':raw,'traced':traced},indent=2)+'\n'}
    for target,text in outputs.items():
        if a.check: assert target.read_text()==text,'regenerate '+str(target)
        else: target.parent.mkdir(parents=True,exist_ok=True);target.write_text(text)
    print('PASS: image/spans, balanced full CFG and first-positive-entry-only clip region')
if __name__=='__main__': main()
