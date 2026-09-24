#!/usr/bin/env python3
"""Differential test of the native BSP segment cast (recomp/kernel/xk_native_1721b0.c) against the lifted guest bodies
f_00088E90 + f_00088B80 + f_000889E0 + f_0017ADD0 + f_00086E20 of a stage and the kernel's xv_bsp_plane_interval
(generated code is not in the repository).

  tools/test_native_1721b0.py <stage>/recomp [cases] [--cc CC] [--seed N] [--verify] [--mutants N] [--bench N]
                               [--replay FILE --reps N] [--extra FLAGS] [--keep PATH]

The stage's shards are only read. The native is compiled against the stage's own xv_x86rt.h three ways - plain page
table -O2, host per-thread table + render view -O2 (the stage's configuration) and -O0 - and runs the randomized
cases in each. --verify also runs the in-game verify mode (mode 1) per case. --mutants N builds deliberately broken
natives and requires each to be caught within N cases (a crash or a hang counts). --bench N runs game-like casts N
times each (guest vs native ns/call, perf-counter instructions). --replay FILE: casts captured in the game with
XV_NATIVE_1721B0_CAPTURE (a cross build with --cc arm-...-gcc --keep only builds)."""
import argparse, os, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FUNCS = ['00088E90', '00088B80', '000889E0', '0017ADD0', '00086E20']
def body(src, fn):
    m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, src, re.M)
    if not m: return None
    return src[m.start():src.index('\n}\n', m.end()) + 3]
def kernel_fn(src, name):
    m = re.search(r'^void %s\(xctx \*c\)\n\{\n' % name, src, re.M)
    if not m: return None
    return src[m.start():src.index('\n}\n', m.end()) + 3]

# Deliberately broken natives: each must produce a mismatch.
MUTANTS = [
    ('plane distance association', 'origin = (s->P[2] * (double)nr_ldf(hp + 8) + s->P[1] * (double)nr_ldf(hp + 4)) + (double)nr_ldf(hp) * s->P[0];',
     'origin = s->P[2] * (double)nr_ldf(hp + 8) + (s->P[1] * (double)nr_ldf(hp + 4) + (double)nr_ldf(hp) * s->P[0]);'),
    ('delta from the point', 'delta = (s->D[2] * (double)nr_ldf(hp + 8) + s->D[1] * (double)nr_ldf(hp + 4)) + (double)nr_ldf(hp) * s->D[0];',
     'delta = (s->D[2] * (double)nr_ldf(hp + 8) + s->D[1] * (double)nr_ldf(hp + 4)) + (double)nr_ldf(hp) * s->P[0];'),
    ('last from the unrounded delta', 'const double last = (double)dl * (double)nr_u2f(t1w) + (double)o;', 'const double last = delta * (double)nr_u2f(t1w) + (double)o;'),
    ('dead slot 2 not written', 'x2 = last; x1 = first;', 'x1 = first;'),
    ('split t not negated', '    x1 = -x1;                                                     /* fchs */', '    (void)0;'),
    ('far child condition ja instead of jnp', 'FCMP(x1, (double)ts, 1);                                      /* fcomp [esp+18h]: depth 0 */\n    FNSTSW(); TEST_AH(0x41);\n    if (!PF()) goto L_88E76;',
     'FCMP(x1, (double)ts, 1);                                      /* fcomp [esp+18h]: depth 0 */\n    FNSTSW(); TEST_AH(0x41);\n    if (ZF()) goto L_88E76;'),
    ('near child: sete on the wrong byte', '    SETF(XK_LOGIC, 0, 0, (uint8_t)ebx, 8);                        /* test bl,bl */\n    LO8(ecx, ZF());',
     '    SETF(XK_LOGIC, 0, 0, (uint8_t)ebx, 8);                        /* test bl,bl */\n    LO8(ecx, !ZF());'),
    ('fsw TOP replaced, not OR-ed', '(fsw & ~0x4700u)', '(fsw & ~0x7F00u)'),
    ('fcom depth', '        FCMP(x1, s->zero, 1);                                     /* fcom [1F0A68] */', '        FCMP(x1, s->zero, 2);'),
    ('leaf list capacity 0xFF', 'SETF(XK_SUB, ecx, 0x100u, ecx - 0x100u, 32);', 'SETF(XK_SUB, ecx, 0xFFu, ecx - 0xFFu, 32);'),
    ('inc al keeps no carry override', 'INC_CF(); LO8(eax, (uint8_t)eax + 1u);', 'LO8(eax, (uint8_t)eax + 1u);'),
    ('and edx,1 without operands', 'SETF(XK_LOGIC, a_, 1u, r_, 32); edx = r_; }   /* and edx,1 */', 'SETF(XK_LOGIC, 0, 0, r_, 32); edx = r_; }   /* and edx,1 */'),
    ('leaf kind rule: previous kind 1 only', '    { const uint8_t a_ = (uint8_t)eax; SETF(XK_SUB, a_, 2u, (uint8_t)(a_ - 2u), 8); }\n    if (!ZF()) goto L_88D41;',
     '    { const uint8_t a_ = (uint8_t)eax; SETF(XK_SUB, a_, 2u, (uint8_t)(a_ - 2u), 8); }\n    goto L_88D41;'),
    ('hit record: surface flags byte skipped', '    nr_w8(m, eax + 0x10u, (uint8_t)edx);', '    (void)0;'),
    ('hit plane pointer: add without flags', 'SETF(XK_ADD, a_, b_, r_, 32); esi = r_; }   /* add esi,edi */', 'esi = r_; (void)a_; (void)b_; }   /* add esi,edi */'),
    ('S+0x20 stores the wrong kind', '    SW8(0x20u, ebx); s->s20 = (uint8_t)ebx;                       /* mov [ebp+20h],bl */\n    ebp = L->sv_ebp;',
     '    SW8(0x20u, 3); s->s20 = 3;\n    ebp = L->sv_ebp;'),
    ('S+0x1C not tracked', '    SW32(0x1Cu, esi); s->s1c = esi;                               /* mov [ebp+1Ch],esi */\n    SW8(0x20u, ebx); s->s20 = (uint8_t)ebx;                       /* mov [ebp+20h],bl */\n    goto L_88E76;',
     '    SW32(0x1Cu, esi);\n    SW8(0x20u, ebx); s->s20 = (uint8_t)ebx;                       /* mov [ebp+20h],bl */\n    goto L_88E76;'),
    ('leaf count not tracked', 'nr_w32(m, eax + 0x14u, v_); s->rcount = v_; }', 'nr_w32(m, eax + 0x14u, v_); }'),
    ('level stack: t1 of the caller not restored', 'E = L->E; t0w = L->t0w; t1w = L->t1w;', 'E = L->E; t0w = L->t0w;'),
    ('sbb as real x86 (the translation keeps the stale carry)', '{ const uint32_t cf_ = CF(), r_ = ecx - ecx - cf_;', '{ const uint32_t cf_ = ecx != 0, r_ = ecx - ecx - cf_;'),
    ('projected v from the u slot', 'x1 = eax < 3u ? (double)(eax == 0u ? p0 : eax == 1u ? p1 : p2) : nr_rf(m, Q + eax * 4u + 0x20u);',
     'x1 = eax < 3u ? (double)(eax == 0u ? p0 : eax == 1u ? p1 : p2) : nr_rf(m, Q + eax * 4u + 0x1Cu);'),
    ('17ADD0 back-edge not counted', '                    be++;                                         /* jns 17ADE0 (a back-edge) */', '                    (void)0;'),
    ('17ADD0 side test sign', '                    if (!ZF()) { SETF(XK_LOGIC, eax, eax, 0, 32); eax = 0; }   /* xor eax,eax */\n                    else eax = 1u;\n                    eax = nr_r32(m, ecx + eax * 4u + 0xCu);',
     '                    if (ZF()) { SETF(XK_LOGIC, eax, eax, 0, 32); eax = 0; }   /* xor eax,eax */\n                    else eax = 1u;\n                    eax = nr_r32(m, ecx + eax * 4u + 0xCu);'),
    ('86E20 cross product operand swap', '        x2 = x2 * x1;                                             /* fmul st,st(1) */', '        x2 = x1 * x2 * 1.0000001;'),
    ('86E20 fstp st(2) not modelled', '        x1 = x3;                                                  /* fstp st(2): depth 2; fstp st(0): depth 1 */', '        x2 = x3;'),
    ('86E20 back-edge not counted', '        be++;                                                     /* jmp 86EA0 (a back-edge) */', '        (void)0;'),
    ('889E0 reference back-edge not counted', '        be++;                                                     /* jl 88A10 (a back-edge) */', '        (void)0;'),
    ('889E0 dominant axis tie', '        FCMP(x3, x2, 3);                                          /* fcom */', '        FCMP(x2, x3, 3);'),
    ('88E90: t limit 1.0 literal', 'else FW32(F + 0x2Cu, 0x3F800000u);', 'else FW32(F + 0x2Cu, 0x3F800001u);'),
    ('layout: record allowed in the upper frames', 'if (rec > 0xFFFFFFFFu - 0x418u || nr_ovl(rec, 0x418u, lo, wl)) return 0;', 'if (rec > 0xFFFFFFFFu - 0x418u || nr_ovl(rec, 0x418u, lo, 0x8000u)) return 0;'),
    ('layout: vectors allowed over the record', ' || nr_ovl(ra[i], rl[i], rec, 0x418u)) return 0;', ') return 0;'),
    ('layout: the record\'s t across a page end', '    if ((rec & 0xFFFu) > 0xFFCu) return 0;', '    (void)0;'),
    ('delegation: its back-edges not counted', '        be += (uint32_t)((1 << 30) - c->preempt);', '        (void)0;'),
    ('delegation: stale carry cell not passed', 'c->f_cf_override = 0; c->f_cf = fcf; c->f_of_override = 0; c->f_of = fof;', 'c->f_cf_override = 0; c->f_cf = 0; c->f_of_override = 0; c->f_of = fof;'),
]

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('recomp'); ap.add_argument('cases', nargs='?', default='3000')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc')); ap.add_argument('--seed', default='1')
    ap.add_argument('--verify', action='store_true'); ap.add_argument('--mutants', type=int, default=0)
    ap.add_argument('--variants', default='all'); ap.add_argument('--bench', type=int, default=0)
    ap.add_argument('--replay', help='casts captured with XV_NATIVE_1721B0_CAPTURE (exactness; speed with --reps)'); ap.add_argument('--reps', type=int, default=0)
    ap.add_argument('--extra', default='', help='extra compiler flags (e.g. -mthumb -march=armv7-a -mfpu=neon -mfloat-abi=hard -static for the Pi)')
    ap.add_argument('--native', default=str(ROOT / 'recomp/kernel/xk_native_1721b0.c'))
    ap.add_argument('--keep', help='also write the test executable here (e.g. to copy it to another machine)')
    a = ap.parse_args(); rec = Path(a.recomp)
    texts = {}
    for p in sorted(rec.glob('code_*.c')):
        t = p.read_text(errors='replace')
        for fn in FUNCS:
            if fn not in texts and re.search(r'^void f_%s\(xctx' % fn, t, re.M): texts[fn] = t
    missing = [fn for fn in FUNCS if fn not in texts]
    if missing: sys.exit(f'not found in {rec}: {missing}')
    geo = (rec / 'kernel/xk_geometry.c').read_text(errors='replace')
    interval = kernel_fn(geo, 'xv_bsp_plane_interval')
    if not interval: sys.exit('xv_bsp_plane_interval not found in kernel/xk_geometry.c')
    pre = texts['00088E90']; preamble = pre[:pre.index('\nvoid f_')]
    bodies = []
    for fn in FUNCS:
        b = body(texts[fn], fn)
        if 'xv_native_1721b0_ray' in b: sys.exit(f'f_{fn} of {rec} carries the native hook: point the test at an unpatched stage copy')
        bodies.append(b)
    # the kernel helper first: it uses xv_x86rt.h's X_G, the shard preamble then redefines X_G for the bodies
    guest = '#include "xv_x86rt.h"\n#include "xv_phase.h"\n' + interval + '\n' + preamble + '\n' + '\n'.join(bodies)
    variants = {'plain -O2': ['-O2'], 'thread-table+render-view -O2': ['-O2', '-DXV_THREAD_PAGE_TABLE=1', '-DXV_RENDER_VIEW=1'],
                'plain -O0': ['-O0']}
    if a.variants != 'all': variants = {k: v for k, v in variants.items() if k.split()[0] in a.variants.split(',')}
    rc = 0
    with tempfile.TemporaryDirectory(prefix='xita-native-1721b0-') as d:
        d = Path(d); (d / 'guest.c').write_text(guest); (d / 'kernel').mkdir()
        native_src = Path(a.native).read_text()
        def build(exe, flags, native_text):
            (d / 'kernel/xk_native_1721b0.c').write_text(native_text)
            cmd = [a.cc, *flags, *a.extra.split(), "-std=gnu11", "-w", "-fno-strict-aliasing", "-ffp-contract=off", "-DXV_NATIVE_1721B0=1", "-DXV_NATIVE_1721B0_TEST=1", '-I' + str(rec),
                   '-I' + str(rec / 'kernel'), str(ROOT / 'tools/tests/native_1721b0.c'), str(d / 'guest.c'),
                   str(d / 'kernel/xk_native_1721b0.c'), '-lm', '-o', str(exe)]
            subprocess.run(cmd, check=True)
        if a.replay:
            exe = d / 'replay'; build(exe, variants.get('thread-table+render-view -O2', ['-O2']), native_src)
            if a.keep: subprocess.run(['cp', str(exe), a.keep], check=True)
            if a.cc != "cc" and "arm" in a.cc: print("built", exe); sys.exit(0)   # cross build: run it on the target (--keep)
            r = subprocess.run([str(exe), "0", "0", "--replay", a.replay, str(a.reps)], capture_output=True, text=True)
            print(r.stdout.strip() + r.stderr.strip()); sys.exit(r.returncode)
        if a.bench:
            exe = d / 'bench'; build(exe, variants.get('thread-table+render-view -O2', ['-O2']), native_src)
            if a.keep: subprocess.run(['cp', str(exe), a.keep], check=True)
            r = subprocess.run([str(exe), a.cases, a.seed, '--bench', str(a.bench)], capture_output=True, text=True)
            print(r.stdout.strip() + r.stderr.strip()); sys.exit(r.returncode)
        for name, flags in variants.items():
            exe = d / 'test'; build(exe, flags, native_src)
            if a.keep: subprocess.run(['cp', str(exe), a.keep + '-' + name.split()[0]], check=True)
            args = [str(exe), a.cases, a.seed] + (['--verify'] if a.verify else [])
            r = subprocess.run(args, capture_output=True, text=True)
            print(f'[{name}] ' + (r.stdout.strip() + r.stderr.strip()).replace('\n', f'\n[{name}] '), flush=True)
            rc |= r.returncode
        if a.mutants:
            caught = 0
            mutants = MUTANTS
            only = os.environ.get('NR_MUTANT_ONLY')
            if only: mutants = [m for m in mutants if only in m[0]]
            for name, old, new in mutants:
                if native_src.count(old) != 1: print(f'[mutant] {name}: pattern not found once ({native_src.count(old)})'); rc |= 1; continue
                exe = d / 'mutant'; build(exe, ['-O2'], native_src.replace(old, new))
                try:
                    r = subprocess.run([str(exe), str(a.mutants), a.seed], capture_output=True, text=True, timeout=3600)
                    m = re.search(r'(\d+) mismatches', r.stdout)
                    n = int(m.group(1)) if m else -1
                    how = f'{n} of {a.mutants} cases differ' if m else f'no summary (exit {r.returncode}: the mutant crashed)'
                except subprocess.TimeoutExpired:
                    n, how = -1, 'no summary (the mutant never finished)'
                ok = n != 0
                caught += ok
                print(f'[mutant] {name}: {how}' + ('' if ok else '  <-- NOT CAUGHT'), flush=True)
            print(f'[mutant] {caught} of {len(mutants)} mutants caught')
    sys.exit(rc)
if __name__ == '__main__': main()
