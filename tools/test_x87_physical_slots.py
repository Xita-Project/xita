#!/usr/bin/env python3
"""Compile differential physical-slot tests; generated C stays outside Git.
Tests slot movement and call mutation, not Halo control flow or x87 arithmetic.
"""
import argparse
from pathlib import Path
import shlex
import subprocess
import sys
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from recompiler.x87_regs import Ctx, Plan, State, slot_name


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--cc', default='cc')
    ap.add_argument('--extra', default='')
    ap.add_argument('--build-only', action='store_true')
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    assert slot_name(-1) == 'xrn1' and slot_name(8) == 'xr8'
    assert slot_name(-1, True) == 'xr7' and slot_name(8, True) == 'xr0'
    p = Plan(SimpleNamespace(entry=0)); p.physical = True
    p.lo, p.hi, p.fsw = -20, 24, True
    assert list(p.slots) == list(range(8))
    ctx = Ctx(0, physical=True)
    dirty = set(); mem = 0
    lines = p.prologue()
    def sync(mutate=False):
        nonlocal mem
        lines.append(p.spill(State(ctx.d, mem, frozenset(dirty), False)))
        lines.append('if (!equal(c,&ref)) return 1;')
        dirty.clear(); mem = ctx.d
        if mutate:
            lines.append('for(unsigned j=0;j<8;j++){ uint64_t w=next(&seed); memcpy(&c->st[j],&w,8); memcpy(&ref.st[j],&w,8); }')
            lines.append('c->fsw ^= 0x4500; ref.fsw ^= 0x4500;')
            lines.append(p.fill())
    # Traverse both directions across multiple physical wraps, spilling aliases
    # together. Call mutation must replace even formerly popped local slots.
    for count, pops in [(19, 5), (3, 23), (17, 2), (4, 13)]:
        for _ in range(count):
            lines.append('{ uint64_t w=next(&seed); double value; memcpy(&value,&w,8);')
            lines.append(ctx.push('value'))
            dirty.update(ctx.written); ctx.written.clear()
            lines.append('ref.fsp=(ref.fsp-1)&7; memcpy(&ref.st[ref.fsp],&w,8); }')
        sync(True)
        for _ in range(pops):
            ctx.pop(); lines.append('ref.fsp=(ref.fsp+1)&7;')
        # Assignment through a logical alias must update the same physical local.
        target = ctx.stw(3); source = ctx.st(0)
        lines.append(f'{target} = {source};')
        dirty.update(ctx.written); ctx.written.clear()
        lines.append('ref.st[(ref.fsp+3)&7]=ref.st[ref.fsp];')
        sync(True)
    sync()
    src = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { double st[8]; uint32_t fsp; uint16_t fsw; } Context;
static uint64_t next(uint64_t *s){ *s ^= *s<<13; *s ^= *s>>7; *s ^= *s<<17; return *s; }
static int equal(Context *a,Context *b){return !memcmp(a->st,b->st,sizeof a->st)&&a->fsp==b->fsp&&a->fsw==b->fsw;}
static int run(unsigned top,uint64_t seed){
Context actual={0},ref={0},*c=&actual;
for(unsigned j=0;j<8;j++){uint64_t w=next(&seed);memcpy(&actual.st[j],&w,8);}
actual.fsp=top;actual.fsw=0x4100;ref=actual;
''' + '\n'.join(lines) + r'''
return 0;
}
int main(void){for(unsigned top=0;top<8;top++)for(unsigned seed=1;seed<=2048;seed++)
if(run(top,seed)){fprintf(stderr,"mismatch top=%u seed=%u\n",top,seed);return 1;}
puts("PASS: 16384 physical-slot cases, wrap, alias spills, popped slots and call mutation");return 0;}
'''
    path = args.output/'physical-slots.c'; path.write_text(src)
    exe = args.output/'physical-slots-test'
    subprocess.run([args.cc, '-std=c11', '-O2', '-fno-strict-aliasing', '-ffp-contract=off',
                    *shlex.split(args.extra), str(path), '-o', str(exe)], check=True)
    if not args.build_only:
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
