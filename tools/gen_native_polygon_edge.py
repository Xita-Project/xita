#!/usr/bin/env python3
"""Generate exact-state dirty-slot locals for the Halo polygon-edge routine.

The input is the user's supported XBE, never another project's implementation.
Prove x87 depth at control-flow joins; retain memory order, float rounding,
all observable context and each original preemption boundary.
"""
from pathlib import Path
import argparse
import hashlib
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks

ENTRY, SIZE = 0xB77C0, 0xC3
DIGEST = "5ceeee6fc591265ef9a0e51d3cd0a0cac96b0a100d27b3e29a1e9a10be4aba73"
IMAGE_DIGEST = "4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae"
FIELDS = ('f_kind', 'f_op1', 'f_op2', 'f_res', 'f_bits', 'f_cf_override',
          'f_cf', 'f_of_override', 'f_of', 'fsw')


def prove_dirty_slots(instructions):
    """Prove no incoming FP reads, including a fresh scheduler reentry."""
    todo = [(ENTRY, 0, frozenset()), (0xB77E5, 0, frozenset())]
    seen, reads, writes, returns, depths = set(), set(), set(), set(), {}
    while todo:
        pc, depth, defined = todo.pop()
        if (pc, depth, defined) in seen:
            continue
        seen.add((pc, depth, defined))
        ins = instructions[pc]
        name = r.MN[ins.mnemonic]
        assert pc not in depths or depths[pc] == depth
        depths[pc] = depth
        rd, wr, delta = set(), set(), 0
        def slot(n):
            return (depth + n) & 7
        if name == 'fld':
            if ins.op0_kind == r.OpKind.REGISTER:
                rd.add(slot(ins.op0_register - r.Register.ST0))
            wr.add(slot(-1))
            delta = -1
        elif name in ('fmul', 'fsub'):
            rd.add(slot(0)); wr.add(slot(0))
            if ins.op_count == 2 and ins.op1_kind == r.OpKind.REGISTER:
                rd.add(slot(ins.op1_register - r.Register.ST0))
        elif name in ('faddp', 'fsubp'):
            rd.update((slot(0), slot(1))); wr.add(slot(1))
            delta = 1
        elif name in ('fst', 'fstp'):
            rd.add(slot(0))
            if ins.op0_kind == r.OpKind.REGISTER:
                wr.add(slot(ins.op0_register - r.Register.ST0))
            delta = int(name == 'fstp')
        elif name == 'fxch':
            rd.update((slot(0), slot(1))); wr.update(rd)
        elif name in ('fcom', 'fcomp', 'fcompp'):
            rd.add(slot(0))
            if name == 'fcompp':
                rd.add(slot(1)); delta = 2
            elif name == 'fcomp':
                delta = 1
        elif name != 'fnstsw':
            assert not name.startswith('f'), name
        assert not rd - defined, (hex(pc), 'incoming x87 value read', rd - defined)
        reads.update(rd); writes.update(wr)
        defined |= wr
        depth += delta
        if ins.flow_control == r.FlowControl.RETURN:
            assert depth == 0
            returns.add((pc, tuple(sorted(defined))))
        elif ins.flow_control == r.FlowControl.CONDITIONAL_BRANCH:
            todo.extend(((ins.next_ip, depth, defined), (ins.near_branch_target, depth, defined)))
        elif ins.flow_control == r.FlowControl.UNCONDITIONAL_BRANCH:
            todo.append((ins.near_branch_target, depth, defined))
        else:
            assert ins.flow_control == r.FlowControl.NEXT
            todo.append((ins.next_ip, depth, defined))
    assert len(depths) == len(instructions) == 79
    assert reads == writes == set(range(2, 8))
    assert depths[0xB7874] == 0
    assert {slots for _, slots in returns} == {(7,), (2, 3, 4, 5, 6, 7)}


def generate(xbe=None, manifest=None):
    if not __debug__:
        raise RuntimeError("generation requires assertions; do not use python -O")
    img = r.Image(str(xbe or ROOT/'haloce/default.xbe'),
                  str(manifest or ROOT/'local/halo_ce_3925/game_manifest.json'))
    assert hashlib.sha256(img.data).hexdigest() == IMAGE_DIGEST, "unsupported image"
    code = img.bytes_at(ENTRY, SIZE)
    assert len(code) == SIZE
    digest = hashlib.sha256(code).hexdigest()
    assert digest == DIGEST, "unsupported polygon edge span"
    instructions = {i.ip: i for i in r.Decoder(32, code, ip=ENTRY)}
    deltas = {'fld': -1, 'fstp': 1, 'faddp': 1, 'fsubp': 1, 'fcomp': 1, 'fcompp': 2}
    allowed = set(deltas) | {'fst', 'fmul', 'fsub', 'fnstsw', 'fxch', 'fcom'}
    depths, todo = {}, [(ENTRY, 0)]
    while todo:
        pc, depth = todo.pop()
        if pc in depths:
            assert depths[pc] == depth, f'inconsistent x87 depth at {pc:x}'
            continue
        depths[pc] = depth
        ins = instructions[pc]
        name = r.MN[ins.mnemonic]
        assert not name.startswith('f') or name in allowed, (hex(pc), name)
        depth += deltas.get(name, 0)
        assert -7 <= depth <= 0
        fc = ins.flow_control
        if fc == r.FlowControl.RETURN:
            assert depth == 0
        elif fc == r.FlowControl.UNCONDITIONAL_BRANCH:
            todo.append((ins.near_branch_target, depth))
        elif fc == r.FlowControl.CONDITIONAL_BRANCH:
            todo.extend(((ins.near_branch_target, depth), (ins.next_ip, depth)))
        else:
            assert fc == r.FlowControl.NEXT, 'leaf must not call other functions'
            todo.append((ins.next_ip, depth))
    assert not set(instructions) - depths.keys()
    disc = r.Discovery(img, {}, img.kernel_imports(), lambda *args: None)
    disc.add_root(ENTRY)
    disc.lift_function(disc.functions[ENTRY])
    disc.split_blocks(disc.functions[ENTRY])
    emitter = r.Emitter(img, disc, {}, img.kernel_imports(), 'unused', 1,
                        hooks=HaloHooks(img))
    hooked = emitter.emit_function(disc.functions[ENTRY])
    hook = ('#ifdef XV_NATIVE_POLYGON_EDGE\n'
            '    { extern int xv_math_polygon_edge(xctx *); if (xv_math_polygon_edge(c)) return; }\n'
            '#endif\n')
    assert hooked.count(hook) == 1
    original_read = img.bytes_at
    def changed(address, size):
        data = bytearray(original_read(address, size))
        if address == ENTRY and size == SIZE:
            data[-1] ^= 1
        return bytes(data)
    img.bytes_at = changed
    try:
        original = emitter.emit_function(disc.functions[ENTRY])
    finally:
        img.bytes_at = original_read
    assert 'xv_math_polygon_edge' not in original
    assert hooked.replace(hook, '') == original
    prove_dirty_slots(instructions)
    # The complete ordinary lift remains the independent test oracle.
    body, depth = [], 0
    for line in original.splitlines()[original.splitlines().index('L_000B77C0:'):]:
        if line == 'L_000B77C0:':
            body.extend([line, '    fp=c->fsp; EDGE_LOAD();'])
            continue
        if line == 'L_000B77E2:':
            body.extend([line, '    fp_wide=1;'])
            continue
        match = re.search(r'/\* ([0-9A-F]{8}) ', line)
        if match:
            depth = depths[int(match[1], 16)]
        if 'x87_compare(' in line:
            body.append(f'    flags.fsp = (fp + {depth & 7}) & 7u;')
            line = line.replace('x87_compare(c,', 'x87_compare(&flags,')
        line = re.sub(r'X_ST\((\d)\)', lambda m: f's{(depth + int(m[1])) & 7}', line)
        match = re.fullmatch(r'    x87_push\(c, (.*)\);', line)
        if match:
            depth -= 1
            line = f'    s{depth & 7} = {match[1]};'
        elif line.strip() == 'x87_pop(c);':
            depth += 1
            continue
        line = re.sub(r'c->r\[([0-7])\]', r'r\1', line)
        for field in FIELDS:
            line = line.replace('c->'+field, 'flags.'+field)
        for macro in ('R16', 'R8L', 'R8H', 'PUSH32', 'POP32', 'FLAGS'):
            line = line.replace('X_'+macro+'(', 'EDGE_'+macro+'(')
        line = re.sub(r'(XF_[A-Z]+)\(c\)', r'\1(&flags)', line)
        line = line.replace('X_PREEMPT()', f'EDGE_PREEMPT({depth & 7})')
        line = line.replace('return;', f'EDGE_SAVE({depth & 7}); return;')
        body.append(line)
    text = '\n'.join(body)
    assert not re.search(r'c->|X_ST\(|x87_push\(|x87_pop\(', text.replace('fp=c->fsp;', ''))
    helpers = set(re.findall(r'\b(\w+)\(c(?:,|\))', text))
    assert helpers <= {'x87_load_f32', 'x87_store_f32'}, helpers
    load = ' '.join([*(f'r{i}=c->r[{i}];' for i in range(8)),
                    *(f'flags.{f}=c->{f};' for f in FIELDS)])
    save = ' '.join([*(f'c->r[{i}]=r{i};' for i in range(8)),
                    'if (fp_wide) { '+ ' '.join(f'c->st[(fp+{i})&7u]=s{i};' for i in range(2,7)) + ' }',
                    'c->st[(fp+7)&7u]=s7;',
                    *(f'c->{f}=flags.{f};' for f in FIELDS)])
    header = f'''/* Generated by tools/gen_native_polygon_edge.py; do not edit.
 * Exact Halo 3925 routine {ENTRY:X}..{ENTRY+SIZE-1:X}, SHA256 {digest}.
 * Native register/FP locals; original memory, flags and scheduling semantics.
 */
#include "xv_x86rt.h"
#include "kernel/xk_polygon_edge.h"
#define EDGE_LOAD() do {{ {load} }} while (0)
#define EDGE_SAVE(d) do {{ {save} c->fsp=(fp+(d))&7u; }} while (0)
#define EDGE_PREEMPT(d) do {{ if (--c->preempt<=0) {{ EDGE_SAVE(d); xv_preempt(c); fp=(c->fsp-(d))&7u; EDGE_LOAD(); }} }} while (0)
#define EDGE_R16(i) (*(uint16_t *)&r##i)
#define EDGE_R8L(i) (*(uint8_t *)&r##i)
#define EDGE_R8H(i) (*((uint8_t *)&r##i+1))
#define EDGE_PUSH32(v) do {{ uint32_t v__=(uint32_t)(v); r4-=4; X_M32(r4)=v__; }} while (0)
#define EDGE_POP32() ({{ uint32_t v__=X_M32(r4); r4+=4; v__; }})
#define EDGE_FLAGS(kind,op1,op2,res,bits) do {{ flags.f_kind=(kind); flags.f_op1=(uint32_t)(op1); flags.f_op2=(uint32_t)(op2); flags.f_res=(uint32_t)(res); flags.f_bits=(bits); flags.f_cf_override=0; flags.f_of_override=0; }} while (0)
static void __attribute__((noinline)) polygon_edge_native(xctx *restrict c)
{{
    unsigned fp=c->fsp;
    int fp_wide=0;
    xctx flags;
    uint32_t {', '.join(f'r{i}' for i in range(8))};
    double {', '.join(f's{i}' for i in range(2,8))};
    uint8_t *const xram_=g_xram; const uint32_t *const xpt_=g_xpt; (void)xram_; (void)xpt_;
    uint8_t *const imgb_=g_img_base; (void)imgb_;
    uint32_t fk_a=0,fk_b=0,fk_r=0; (void)fk_a;(void)fk_b;(void)fk_r;
'''
    wrapper = """
/* Keep disabled calls out of the native FP/register frame. */
int xv_math_polygon_edge(xctx *c)
{
    if (!xv_polygon_edge_begin()) return 0;
    polygon_edge_native(c);
    xv_polygon_edge_end();
    return 1;
}
"""
    return header+'\n'+text+'\n'+wrapper, original+'\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    source, reference = generate(args.xbe, args.manifest)
    outputs = {ROOT/'recomp/kernel/xk_polygon_edge.c': source,
               ROOT/'recomp/host/build/original_000B77C0.c': '#include "xv_x86rt.h"\n'+reference}
    for target, text in outputs.items():
        if args.check:
            assert target.read_text() == text, str(target)+' needs regeneration'
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text)
    print('PASS: image/span, full x87 CFG, scheduler dirty slots, unchanged original fallback')
