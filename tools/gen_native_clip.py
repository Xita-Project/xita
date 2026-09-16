#!/usr/bin/env python3
"""Lower the exact Halo 3925 polygon clipper's x87 stack to native locals.

Retain the lift's integer/flag and guest-memory semantics. Prove the x87 depth
at every control-flow join before replacing dynamic stack indexing. Regenerate
with the same iced-x86 environment used by xita_recomp.py; --check is read-only.
"""
from pathlib import Path
import hashlib
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks

ENTRY, SIZE = 0xB71C0, 874
DIGEST = '34bf76203325f8157fcc9595b80b4be8851d829baabc556d45941533aa6c22e3'

IMAGE_SHA256 = '4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae'
ARM_OPERAND_ORDER = r"""/* Operand priority pinned to the audited VitaSDK original lift.
 * No data scan, NaN normalization or conditional arithmetic path.
 * Keep non-ARM source expressions exactly as the existing implementation. */
#ifdef __arm__
static inline double clip_ordered_add(double a,double b) {
 double out; __asm__ volatile("vadd.f64 %P0, %P1, %P2" : "=w"(out) : "w"(b), "w"(a)); return out;
}
static inline double clip_ordered_mul(double a,double b) {
 double out; __asm__ volatile("vmul.f64 %P0, %P1, %P2" : "=w"(out) : "w"(b), "w"(a)); return out;
}
#else
#define clip_ordered_add(a,b) ((a)+(b))
#define clip_ordered_mul(a,b) ((a)*(b))
#endif
"""


def ordered_arm_fp_body(body):
    """Pin the audited original VitaSDK lift's NaN operand priority.

    No finite-input scan or value canonicalization. Other hosts retain their
    existing C expressions, whose inherited strict NaN differences are separate.
    Match exact instruction sites/counts after the image/CFG checks, before the
    integer-register lowering duplicates this body into both native modes.
    """
    sites = {0xB723A: ('+', 'add', 2), 0xB728A: ('+', 'add', 1),
             0xB7306: ('+', 'add', 2), 0xB7314: ('+', 'add', 2),
             0xB735D: ('*', 'mul', 1)}
    seen = dict.fromkeys(sites, 0)
    result, pc = [], None
    for line in body:
        marker = re.search(r'/\* ([0-9A-F]{8}) ', line)
        if marker:
            pc = int(marker[1], 16)
        match = re.fullmatch(r'    (s[0-7]) = (s[0-7]) ([+*]) (s[0-7]);', line)
        if pc in sites and match:
            operator, helper, count = sites[pc]
            assert match[3] == operator, (hex(pc), line)
            line = f'    {match[1]} = clip_ordered_{helper}({match[2]}, {match[4]});'
            seen[pc] += 1
        result.append(line)
    assert seen == {pc: spec[2] for pc, spec in sites.items()}, seen
    return result


def register_body(body):
    """Keep integer registers local, publishing at every context boundary.

    The audited shifts only use/update flags. Memory and x87 helpers never
    inspect integer registers. String moves, the probe and preemption do.
    Reject new context-taking helpers until their effects are classified.
    """
    text = '\n'.join(body)
    helpers = set(re.findall(r'\b(\w+)\(c(?:,|\))', text))
    assert helpers <= {'XF_Z', 'XF_S', 'XF_O', 'XF_C', 'XF_P', 'x_shl32', 'x_shr32',
                       'x87_load_f32', 'x87_store_f32', 'x87_compare',
                       'x_str_movs', 'f_0001D130', 'CLIP_TRACE_ALLOWED'}, helpers
    result = []
    for line in body:
        line = re.sub(r'c->r\[([0-7])\]', r'r\1', line)
        for macro in ('R16', 'R8L', 'R8H', 'PUSH32', 'POP32'):
            line = line.replace('X_' + macro + '(', 'CLIP_' + macro + '(')
        if line.strip() == 'f_0001D130(c);':
            result.append('    CLIP_REG_SAVE();')
        result.append(line)
        if 'x_str_movs(c,' in line:
            result.append('    CLIP_REG_LOAD();')
    assert 'c->r[' not in '\n'.join(result)
    return result


def generate():
    if not __debug__: raise RuntimeError("generation requires assertions")
    img = r.Image(str(ROOT/'haloce/default.xbe'), str(ROOT/'local/halo_ce_3925/game_manifest.json'))
    assert hashlib.sha256(img.data).hexdigest() == IMAGE_SHA256
    code = img.bytes_at(ENTRY, SIZE)
    assert hashlib.sha256(code).hexdigest() == DIGEST
    instructions = {i.ip: i for i in r.Decoder(32, code, ip=ENTRY)}
    delta = {'fld': -1, 'fstp': 1, 'faddp': 1, 'fdivp': 1,
             'fcomp': 1, 'fcompp': 2}
    allowed = set(delta) | {'fabs', 'fadd', 'fchs', 'fcom', 'fmul',
                            'fnstsw', 'fsub', 'fsubr', 'fxch'}
    depths, todo = {}, [(ENTRY, 0)]
    while todo:
        pc, depth = todo.pop()
        if pc in depths:
            assert depths[pc] == depth, f'inconsistent x87 depth at {pc:x}'
            continue
        depths[pc] = depth
        ins = instructions[pc]
        name = r.MN[ins.mnemonic]
        assert not name.startswith('f') or name in allowed
        depth += delta.get(name, 0)
        assert -7 <= depth <= 0
        fc = ins.flow_control
        if fc == r.FlowControl.RETURN:
            assert depth == 0
        elif fc == r.FlowControl.UNCONDITIONAL_BRANCH:
            todo.append((ins.near_branch_target, depth))
        elif fc == r.FlowControl.CONDITIONAL_BRANCH:
            todo.extend(((ins.near_branch_target, depth), (ins.next_ip, depth)))
        else:
            assert fc in (r.FlowControl.NEXT, r.FlowControl.CALL)
            if fc == r.FlowControl.CALL:
                # The stack-allocation probe has no x87 or scheduler effects.
                assert ins.near_branch_target == 0x1D130 and depth == 0
            todo.append((ins.next_ip, depth))
    # One unreachable alignment instruction sits between the initial loop
    # branch and its body. All executable bytes are covered by the proof.
    assert {pc: str(i) for pc, i in instructions.items() if pc not in depths} == {0xB7269: 'lea esp,[esp]'}
    disc = r.Discovery(img, {}, img.kernel_imports(), lambda *args: None)
    for address in (ENTRY, 0x1D130):
        disc.add_root(address)
        disc.lift_function(disc.functions[address])
        disc.split_blocks(disc.functions[address])
    emit = r.Emitter(img, disc, {}, img.kernel_imports(), 'unused', 1, hooks=HaloHooks(img))
    hooked = emit.emit_function(disc.functions[ENTRY])
    assert hooked.count('if (xv_math_polygon_clip(c)) return;') == 1
    read = img.bytes_at
    def changed(address, size):
        data = bytearray(read(address, size))
        if address == ENTRY and size == SIZE: data[-1] ^= 1
        return bytes(data)
    img.bytes_at = changed
    try: original = emit.emit_function(disc.functions[ENTRY])
    finally: img.bytes_at = read
    assert 'xv_math_polygon_clip' not in original
    assert re.sub(r'^    \{ extern int xv_math_polygon_clip.*\n', '', hooked, flags=re.M) == original
    probe = emit.emit_function(disc.functions[0x1D130])
    body = []
    depth = 0
    for line in original.splitlines()[4:]:
        match = re.search(r'/\* ([0-9A-F]{8}) ', line)
        if match:
            depth = depths[int(match[1], 16)]
        if 'x87_compare(' in line:
            body.append(f'    c->fsp = (fp + {depth & 7}) & 7u;')
        line = re.sub(r'X_ST\((\d)\)', lambda m: f's{(depth + int(m[1])) & 7}', line)
        match = re.fullmatch(r'    x87_push\(c, (.*)\);', line)
        if match:
            depth -= 1
            line = f'    s{depth & 7} = {match[1]};'
        elif line.strip() == 'x87_pop(c);':
            depth += 1
            continue
        line = line.replace('X_PREEMPT()', f'CLIP_PREEMPT({depth & 7})')
        line = line.replace('return;', f'CLIP_SAVE({depth & 7}); return 1;')
        # String helpers expose the context to diagnostic hooks; retain that
        # boundary even though the audited helper never touches its x87 state.
        if 'x_str_movs(' in line:
            body.append(f'    CLIP_SAVE({depth & 7});')
        body.append(line)
        if line.strip() == 'f_0001D130(c);':
            body.extend([
                '    if (CLIP_TRACE_ALLOWED(c) && &xv_cur_fn) {',
                '        if (&xv_watch_n && xv_watch_n && xv_watch_leave)',
                '            xv_watch_leave(xv_cur_fn, saved_fn, c);',
                '        xv_cur_fn = saved_fn;',
                '    }',
                '    fp = c->fsp; CLIP_LOAD();',
            ])
    assert not any('X_ST(' in s or 'x87_push(' in s or 'x87_pop(' in s for s in body)
    body = ordered_arm_fp_body(body)
    load = ' '.join(f's{i} = c->st[(fp + {i}) & 7u];' for i in range(8))
    save = ' '.join(f'c->st[(fp + {i}) & 7u] = s{i};' for i in range(8))
    header = f'''/* Generated by tools/gen_native_clip.py; do not edit by hand.
 * Halo 3925 B71C0..B7529 SHA256 {DIGEST}
 * Native FP locals; original rounding, memory aliases, flags and preemption.
 */
#include "../xv_x86rt.h"
#include "xk_object_jobs.h"
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
#define CLIP_TRACE_ALLOWED(c) (!xv_is_object_job(c))
#else
#define CLIP_TRACE_ALLOWED(c) 1
#endif
#include <stdlib.h>
extern void f_0001D130(xctx *);
extern volatile uint32_t xv_cur_fn __attribute__((weak));
extern int xv_watch_n __attribute__((weak));
extern void xv_watch_leave(uint32_t, uint32_t, xctx *) __attribute__((weak));
static unsigned clip_calls;
static int clip_enabled=-1, clip_registers=-1, clip_register_override=-1;
static int clip_config(void) {{
    if (clip_registers < 0) {{ const char *e=getenv("XV_CLIP_REGISTERS"); clip_registers=!e || atoi(e)!=0; }}
    if (clip_enabled < 0) {{ const char *e=getenv("XV_NATIVE_CLIP"); clip_enabled=!e || atoi(e)!=0; }}
    return clip_enabled;
}}
unsigned xv_math_clip_calls(void) {{ XV_OBJECT_MATH_GUARD(); unsigned n=clip_calls; clip_calls=0; return n; }}
#define CLIP_SAVE(d) do {{ {save} c->fsp = (fp + (d)) & 7u; }} while (0)
#define CLIP_LOAD() do {{ {load} }} while (0)
#define CLIP_PREEMPT(d) do {{ if (--c->preempt <= 0) {{ CLIP_SAVE(d); xv_preempt(c); fp=(c->fsp-(d)) & 7u; CLIP_LOAD(); }} }} while (0)
int xv_math_polygon_clip(xctx *restrict c)
{{
    XV_OBJECT_MATH_GUARD();
    if (!clip_config()) return 0;
    clip_calls++;
    uint32_t saved_fn = CLIP_TRACE_ALLOWED(c) && &xv_cur_fn ? xv_cur_fn : 0;
    unsigned fp = c->fsp;
    double {', '.join(f's{i}' for i in range(8))};
    uint8_t *const xram_ = g_xram; const uint32_t *const xpt_ = g_xpt; (void)xram_; (void)xpt_;
'''
    # Retain the previous FP-only implementation for a runtime A/B and rollback.
    previous = header.replace('int xv_math_polygon_clip(', 'static int clip_fp_only(')
    integer_save = ' '.join(f'c->r[{i}] = r{i};' for i in range(8))
    integer_load = ' '.join(f'r{i} = c->r[{i}];' for i in range(8))
    native = f'''
#undef CLIP_SAVE
#undef CLIP_LOAD
#define CLIP_REG_SAVE() do {{ {integer_save} }} while (0)
#define CLIP_REG_LOAD() do {{ {integer_load} }} while (0)
#define CLIP_SAVE(d) do {{ CLIP_REG_SAVE(); {save} c->fsp = (fp + (d)) & 7u; }} while (0)
#define CLIP_LOAD() do {{ CLIP_REG_LOAD(); {load} }} while (0)
#define CLIP_R16(i) (*(uint16_t *)&r##i)
#define CLIP_R8L(i) (*(uint8_t *)&r##i)
#define CLIP_R8H(i) (*((uint8_t *)&r##i + 1))
#define CLIP_PUSH32(v) do {{ uint32_t v__ = (uint32_t)(v); r4 -= 4; X_M32(r4) = v__; }} while (0)
#define CLIP_POP32() ({{ uint32_t v__ = X_M32(r4); r4 += 4; v__; }})
static unsigned clip_register_calls;
void xv_clip_registers_override(int enabled) {{ clip_register_override=enabled; }}
unsigned xv_math_clip_register_calls(void) {{ XV_OBJECT_MATH_GUARD(); unsigned n=clip_register_calls; clip_register_calls=0; return n; }}
'''
    entry = header[header.index('int xv_math_polygon_clip('):]
    entry = entry.replace('    if (!clip_config()) return 0;', '    (void)clip_config();\n    if (clip_register_override==0 || (clip_register_override<0 && !clip_registers)) return clip_fp_only(c);\n    if (!clip_enabled) return 0;')
    entry = entry.replace('    clip_calls++;', '    clip_calls++; clip_register_calls++;')
    entry += '    uint32_t '+', '.join(f'r{i}' for i in range(8))+';\n    CLIP_REG_LOAD();\n'
    bridge = """
/* Called only by the optional region. Init/configuration is owner-drained;
 * accounting uses each original clip's math guard, never a widened region lock. */
int xv_clip_region_compatible(void) {
    return clip_config() && (clip_register_override>0 || (clip_register_override<0 && clip_registers));
}
void xv_clip_region_account(void) { clip_calls++; clip_register_calls++; }
"""
    return ARM_OPERAND_ORDER+previous+'\n'.join(body)+'\n'+native+entry+'\n'.join(register_body(body))+'\n'+bridge, original+'\n'+probe+'\n'


if __name__ == '__main__':
    source, reference = generate()
    target = ROOT/'recomp/kernel/xk_clip.c'
    if '--check' in sys.argv:
        assert target.read_text() == source, 'native clipper needs regeneration'
    else:
        target.write_text(source)
    ref = ROOT/'recomp/host/build/original_000B71C0.c'
    ref.parent.mkdir(parents=True, exist_ok=True)
    ref.write_text(reference)
    print('PASS: exact clipper image, complete CFG, balanced x87 joins and reproducible native lowering')
