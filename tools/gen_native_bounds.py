#!/usr/bin/env python3
"""Generate native locals for the exact Halo bounding-box visibility routine.

The input is the user's supported XBE, never another project's implementation.
Prove x87 depth at control-flow joins; retain memory order, float rounding,
all observable context and each original preemption boundary.
"""
from pathlib import Path
import hashlib
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as r
from games.halo_ce_3925.discovery import HaloDiscovery
from games.halo_ce_3925.hooks import HaloHooks

ENTRY, SIZE = 0x5C300, 0x2DD
DIGEST = "5e463d77ea6ed255323f310d40cf3f7e847c08e7a6a71841b12545b1f937e1ab"
FIELDS = ('f_kind', 'f_op1', 'f_op2', 'f_res', 'f_bits', 'f_cf_override',
          'f_cf', 'f_of_override', 'f_of', 'fsw')


def generate():
    img = r.Image(str(ROOT/'haloce/default.xbe'),
                  str(ROOT/'local/halo_ce_3925/game_manifest.json'))
    code = img.bytes_at(ENTRY, SIZE)
    assert hashlib.sha256(code).hexdigest() == DIGEST
    instructions = {i.ip: i for i in r.Decoder(32, code, ip=ENTRY)}
    deltas = {'fld': -1, 'fstp': 1, 'faddp': 1, 'fcomp': 1}
    allowed = set(deltas) | {'fst', 'fmul', 'fsub', 'fnstsw'}
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
    disc = HaloDiscovery(img, {}, img.kernel_imports(), lambda *args: None)
    disc.add_root(ENTRY)
    disc.lift_function(disc.functions[ENTRY])
    disc.split_blocks(disc.functions[ENTRY])
    emitter = r.Emitter(img, disc, {}, img.kernel_imports(), 'unused', 1,
                        hooks=HaloHooks(img))
    hooked = emitter.emit_function(disc.functions[ENTRY])
    assert hooked.count('if (xv_math_bounds(c)) return;') == 1
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
    assert 'xv_math_bounds' not in original
    assert re.sub(r'^    \{ extern int xv_math_bounds.*\n', '', hooked,
                  flags=re.M) == original
    # The complete ordinary lift remains the independent test oracle.
    body, prefix, depth, native = [], [], 0, False
    for line in original.splitlines()[4:]:
        if line == 'L_0005C36B:':
            native = True
            body.extend([line, '    fp=c->fsp; BOUNDS_LOAD();'])
            continue
        if not native:
            prefix.append(line.replace('return;', 'return 1;'))
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
            line = line.replace('X_'+macro+'(', 'BOUNDS_'+macro+'(')
        line = re.sub(r'(XF_[A-Z]+)\(c\)', r'\1(&flags)', line)
        line = line.replace('X_PREEMPT()', f'BOUNDS_PREEMPT({depth & 7})')
        line = line.replace('return;', f'BOUNDS_SAVE({depth & 7}); return 1;')
        body.append(line)
    text = '\n'.join(body)
    assert not re.search(r'c->|X_ST\(|x87_push\(|x87_pop\(', text.replace('fp=c->fsp;', ''))
    helpers = set(re.findall(r'\b(\w+)\(c(?:,|\))', text))
    assert helpers <= {'x87_load_f32', 'x87_store_f32'}, helpers
    load = ' '.join([*(f'r{i}=c->r[{i}];' for i in range(8)),
                    *(f's{i}=c->st[(fp+{i})&7u];' for i in range(8)),
                    *(f'flags.{f}=c->{f};' for f in FIELDS)])
    save = ' '.join([*(f'c->r[{i}]=r{i};' for i in range(8)),
                    *(f'c->st[(fp+{i})&7u]=s{i};' for i in range(8)),
                    *(f'c->{f}=flags.{f};' for f in FIELDS)])
    header = f'''/* Generated by tools/gen_native_bounds.py; do not edit.
 * Exact Halo 3925 routine {ENTRY:X}..{ENTRY+SIZE-1:X}, SHA256 {DIGEST}.
 * Native register/FP locals; original memory, flags and scheduling semantics.
 */
#include "../xv_x86rt.h"
#include <stdlib.h>
static unsigned bounds_calls;
static int bounds_override=-1;
void xv_native_bounds_override(int enabled) {{ bounds_override=enabled; }}
unsigned xv_math_bounds_calls(void) {{ unsigned n=bounds_calls; bounds_calls=0; return n; }}
#define BOUNDS_LOAD() do {{ {load} }} while (0)
#define BOUNDS_SAVE(d) do {{ {save} c->fsp=(fp+(d))&7u; }} while (0)
#define BOUNDS_PREEMPT(d) do {{ if (--c->preempt<=0) {{ BOUNDS_SAVE(d); xv_preempt(c); fp=(c->fsp-(d))&7u; BOUNDS_LOAD(); }} }} while (0)
#define BOUNDS_R16(i) (*(uint16_t *)&r##i)
#define BOUNDS_R8L(i) (*(uint8_t *)&r##i)
#define BOUNDS_R8H(i) (*((uint8_t *)&r##i+1))
#define BOUNDS_PUSH32(v) do {{ uint32_t v__=(uint32_t)(v); r4-=4; X_M32(r4)=v__; }} while (0)
#define BOUNDS_POP32() ({{ uint32_t v__=X_M32(r4); r4+=4; v__; }})
#define BOUNDS_FLAGS(kind,op1,op2,res,bits) do {{ flags.f_kind=(kind); flags.f_op1=(uint32_t)(op1); flags.f_op2=(uint32_t)(op2); flags.f_res=(uint32_t)(res); flags.f_bits=(bits); flags.f_cf_override=0; flags.f_of_override=0; }} while (0)
int xv_math_bounds(xctx *restrict c)
{{
    static int configured=-1;
    if (configured<0) {{ const char *e=getenv("XV_NATIVE_BOUNDS"); configured=e && atoi(e)!=0; }}
    if (!(bounds_override<0 ? configured : bounds_override)) return 0;
    bounds_calls++;
    unsigned fp=c->fsp;
    xctx flags;
    uint32_t {', '.join(f'r{i}' for i in range(8))};
    double {', '.join(f's{i}' for i in range(8))};
    uint8_t *const xram_=g_xram; const uint32_t *const xpt_=g_xpt; (void)xram_; (void)xpt_;
'''
    return header+'\n'.join(prefix)+'\n'+text+'\n', original+'\n'


if __name__ == '__main__':
    source, reference = generate()
    target = ROOT/'recomp/kernel/xk_bounds.c'
    if '--check' in sys.argv:
        assert target.read_text() == source, 'native bounds routine needs regeneration'
    else:
        target.write_text(source)
    target = ROOT/'recomp/host/build/original_0005C300.c'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(reference)
    print('PASS: complete bounds signature, balanced x87 CFG, reproducible native locals and original fallback')
