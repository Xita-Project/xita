#!/usr/bin/env python3
"""Differential test of the native BSP sphere query (recomp/kernel/xk_native_4b9d0.c) against the lifted guest bodies
f_00088110 + f_00087EA0 + f_00087E10 + f_00086F50 + f_000B0CB0 of a stage (generated code is not in the repository).

  tools/test_native_4b9d0.py <stage>/recomp [cases] [--cc CC] [--seed N] [--verify] [--mutants N] [--partial]

The stage's shards are only read. The guest bodies are compiled as translated (their XV_NATIVE_BSP_SPHERE /
COLLISION_TRAVERSAL / COLLISION_VERTICES / SEGMENT_SPHERE blocks off: the plain translation is the reference; the
in-game verify mode compares against the fused copy with those blocks on). The native is compiled against the stage's
own xv_x86rt.h three ways - plain page table -O2, host per-thread table + render view -O2 (the stage's configuration)
and -O0 - and runs the randomized cases in each. --verify also runs the in-game verify mode (mode 1) per case.
--mutants N builds deliberately broken natives and requires each to be caught within N cases (a crash or a hang
counts). --bench N runs game-like queries N times each (guest vs native ns/call, perf-counter instructions)."""
import argparse, os, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FUNCS = ['00088110', '00087EA0', '00087E10', '00086F50', '000B0CB0',
         '000864C0', '00085D10', '00085A00', '00085720', '00011120', '000111A0']
def body(src, fn):
    m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, src, re.M)
    if not m: return None
    return src[m.start():src.index('\n}\n', m.end()) + 3]

# Deliberately broken natives: each must produce a mismatch.
MUTANTS = [
    ('plane distance association', 'x1 = x1 * HF(hc, ecx, 8u, 12);\n            x2 = HF(hp, prec, 4u, 16); x2 = x2 * HF(hc, ecx, 4u, 12);\n            x1 = x1 + x2;',
     'x1 = x1 * HF(hc, ecx, 8u, 12);\n            x2 = HF(hp, prec, 4u, 16); x2 = x2 * HF(hc, ecx, 4u, 12);\n            x1 = x2 + x1 * 1.0000001;'),
    ('vertex distance association', 'sum = sum + e2;                                      /* shufps 0Eh: (e2,e3,e0,e0); addss */\n        sum = sum + e3;',
     'sum = sum + (e2 + e3);\n'),
    ('vertex list capacity 0xFF', 'SETF(XK_SUB, ebp, 0x100u, ebp - 0x100u, 32);', 'SETF(XK_SUB, ebp, 0xFFu, ebp - 0xFFu, 32);'),
    ('fast scan back-edges off by one', 'if (i < n) { SETF(XK_SUB, edx, edx, 0, 32); be += i; goto L_8707E; }', 'if (i < n) { SETF(XK_SUB, edx, edx, 0, 32); be += i + 1u; goto L_8707E; }'),
    ('vertex scan: last element missed', 'const uint32_t n = ebp, i = n4_find(m, eax + 0x80Cu, n, edx);', 'const uint32_t n = ebp, i = n > 1u ? n4_find(m, eax + 0x80Cu, n - 1u, edx) + (n4_find(m, eax + 0x80Cu, n - 1u, edx) == n - 1u) : n4_find(m, eax + 0x80Cu, n, edx);'),
    ('edge scan: not-found back-edges', 'SETF(XK_SUB, n, n, 0, 32); be += n - 1u;\n                } else {\n                    for (;;) {\n                        { uint32_t a_ = n4_lr32(s, m, eax + ecx * 4u + 0x408u);',
     'SETF(XK_SUB, n, n, 0, 32); be += n;\n                } else {\n                    for (;;) {\n                        { uint32_t a_ = n4_lr32(s, m, eax + ecx * 4u + 0x408u);'),
    ('surface scan: first element skipped', 'const uint32_t n = edx, i = n4_find(m, esi + 4u, n, sface);', 'const uint32_t n = edx, i = n > 1u ? n4_find(m, esi + 8u, n - 1u, sface) + 1u : n;'),
    ('ancestor side bit', 'eax |= 0x80000000u;\n                    n4_aw32', 'eax |= 0x40000000u;\n                    n4_aw32'),
    ('fsw TOP replaced, not OR-ed', '(fsw & ~0x4700u)', '(fsw & ~0x7F00u)'),
    ('segment test: fcomp depth', 'FCMP(x4, ZERO(), 4);', 'FCMP(x4, ZERO(), 3);'),
    ('segment test: back-edge not counted', 'if (!PF()) { be++; LO8(eax, 1); esp += 0x18u; goto out; }', 'if (!PF()) { LO8(eax, 1); esp += 0x18u; goto out; }'),
    ('neg/sbb as real x86 (the translation keeps the stale carry)', 'uint32_t cf_ = CF(); uint32_t r_ = ecx - ecx - cf_;', 'uint32_t cf_ = ecx != 0; uint32_t r_ = ecx - ecx - cf_;'),
    ('dead slot: fstp st(1) skipped', 'x1 = x2;                                             /* fstp st(1): depth 1 */', '(void)0;'),
    ('projected point: u/v swapped', 'edx = AXIS(eax + 2u);\n            eax = AXIS(eax);', 'edx = AXIS(eax);\n            eax = AXIS(eax + 2u);'),
    ('xmm0 lane order', 's->xmm0[0] = e3; s->xmm0[1] = e0; s->xmm0[2] = e0; s->xmm0[3] = e2;', 's->xmm0[0] = e0; s->xmm0[1] = e3; s->xmm0[2] = e0; s->xmm0[3] = e2;'),
    ('dirty ignored for the pops', 'edi = CR(sv_edi, n4_r32(m, E - 0x20u));\n    esi = CR(sv_esi, n4_r32(m, E - 0x1Cu));\n    ebp = CR(sv_ebp, n4_r32(m, E - 0x18u));\n    ebx = CR(sv_ebx, n4_r32(m, E - 0x14u));\n    esp = E + 4u;\n    N4_SAVE_ALL();\n}',
     'edi = sv_edi;\n    esi = sv_esi;\n    ebp = sv_ebp;\n    ebx = sv_ebx;\n    esp = E + 4u;\n    N4_SAVE_ALL();\n}'),
    ('wild list store not dirty', '    n4_w32(m, a, v); s->dirty = *D = 1;\n}\nstatic inline __attribute__((always_inline)) uint32_t n4_lr32', '    n4_w32(m, a, v);\n}\nstatic inline __attribute__((always_inline)) uint32_t n4_lr32'),
    ('layout: lists allowed inside the frame', 'if ((res & 3u) || res < E + 12u ||', 'if ((res & 3u) || res < E - 0x200u ||'),
    ('stale carry after the list store', '{ INCDEC_CF(); uint32_t v_ = n4_lr32(s, m, esi) + 1u; n4_lw32(s, m, &D, esi, v_); }', '{ fcfo = 1; fcf = 0; uint32_t v_ = n4_lr32(s, m, esi) + 1u; n4_lw32(s, m, &D, esi, v_); }'),
    ('leaf capacity signed/unsigned', 'SETF(XK_SUB, ecx, 0x100u, ecx - 0x100u, 32);\n        FSW32(E - 0x10u, edi); s10 = edi;\n        if (!(SF() == OF())) {',
     'SETF(XK_SUB, ecx, 0x100u, ecx - 0x100u, 32);\n        FSW32(E - 0x10u, edi); s10 = edi;\n        if (ecx < 0x100u) {'),
]

# Deliberately broken feature-test natives (--part features)
MUTANTS5 = [
    ('sphere: closing-speed sum association', '    x2 = d2; x2 = x2 * HF(hd, dir, 8u, 12u); x1 = x1 + x2;\n    x2 = d1; x2 = x2 * HF(hd, dir, 4u, 12u); x1 = x1 + x2;\n    const float bb',
     '    x2 = d1; x2 = x2 * HF(hd, dir, 4u, 12u); x1 = x1 + x2;\n    x2 = d2; x2 = x2 * HF(hd, dir, 8u, 12u); x1 = x1 + x2;\n    const float bb'),
    ('sphere: radius squared rounded to float', '    x3 = x1; x3 = x3 * x1;                                   /* fld st(1); fmul st,st(2) */\n    x2 = x2 - x3;                                            /* fsubp */\n    const float c0',
     '    x3 = x1; x3 = (float)(x3 * x1);\n    x2 = x2 - x3;\n    const float c0'),
    ('sphere: closing speed not stored over the argument', 'const float bb = N5WF(Ec + 4u, x1);', 'const float bb = (float)x1;'),
    ('sphere: fxch dropped', '    { const double t_ = x2; x2 = x1; x1 = t_; }              /* fxch */\n    x2 = x2 * HF(hd, dir, 8u, 12u);\n    const float m2',
     '    x2 = x2 * HF(hd, dir, 8u, 12u);\n    const float m2'),
    ('normalize: eps compare at depth 1', 'FCMP(x2, s->eps, 2);', 'FCMP(x2, s->eps, 1);'),
    ('normalize: short vector keeps its length', 'if (!PF()) x1 = s->zero;                 /* 1116E', 'if (!PF()) {}                            /* 1116E'),
    ('capsule: t2 clamped to literal 1.0', 'if (ZF()) x1 = s->one;                                   /* t2 > 1', 'if (ZF()) x1 = 1.0;                                      /* t2 > 1'),
    ('capsule: back-edge not counted (t1 > 1)', 'if (ZF()) { be++; al = 0; goto out; }                    /* je 85B2B', 'if (ZF()) { al = 0; goto out; }                    /* je 85B2B'),
    ('capsule: normal scale divides by A', 'x1 = x1 / aa;                                        /* fdiv [esp+14h] = Ec-0x20 */', 'x1 = x1 / A;'),
    ('prism: edge loop one short', 'SETF(XK_SUB, i - 1u, n, i - 1u - n, 32);             /* lea eax,[esi-1]; cmp eax,edi */', 'SETF(XK_SUB, i, n, i - n, 32);'),
    ('prism: last edge does not wrap', 'const uint32_t nx = ((ge - 1u) & i) * 8u;', 'const uint32_t nx = i * 8u; (void)ge;'),
    ('prism: projected direction u/v swapped', 'x1 = u < 3u ? (double)D[u] : n4_rf(m, Ec - 0xCu + u * 4u);', 'x1 = v < 3u ? (double)D[v] : n4_rf(m, Ec - 0xCu + v * 4u);'),
    ('prism: second shl flags', 'ix = SHL32((uint32_t)side + SX16(axis) * 2u, 2u);        /* the same index again', 'ix = SHL32((uint32_t)side + SX16(axis) * 2u, 3u) >> 1;        /* the same index again'),
    ('prism: parallel edge sign test', 'if (!PF()) { be++; goto L_8581F; }\n        } else {', 'if (ZF()) { be++; goto L_8581F; }\n        } else {'),
    ('prism: x87 slot 6 not written', '        x6 = Dv; x6 = x6 * x1;                               /* fld [esp+14h]; fmul st,st(5) */\n        x5 = x5 - x6;',
     '        { const double y6 = Dv * x1; x5 = x5 - y6; }'),
    ('feature test: facing threshold 0', 'FCMP(x1, s->c24, 1); N5_TEST(5);', 'FCMP(x1, s->zero, 1); N5_TEST(5);'),
    ('feature test: inner back-edge not counted', 'if (SF() != OF()) { be++; goto L_86500; }', 'if (SF() != OF()) { goto L_86500; }'),
    ('feature test: capsule record base', '            ecx = esi + edx * 8u + 0x1C08u;', '            ecx = esi + edx * 8u + 0x1C00u;'),
    ('imul: of override not set', 'SETF(XK_LOGIC, 0, 0, r__, 32); fcfo = fofo = 1; fcf = fof = (p__ != (int64_t)(int32_t)r__); r__; })',
     'SETF(XK_LOGIC, 0, 0, r__, 32); fcfo = 1; fcf = fof = (p__ != (int64_t)(int32_t)r__); r__; })'),
]

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('recomp'); ap.add_argument('cases', nargs='?', default='3000')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc')); ap.add_argument('--seed', default='1')
    ap.add_argument('--verify', action='store_true'); ap.add_argument('--mutants', type=int, default=0)
    ap.add_argument('--variants', default='all'); ap.add_argument('--bench', type=int, default=0)
    ap.add_argument('--replay', help='queries captured with XV_NATIVE_4B9D0_CAPTURE (exactness; speed with --reps)'); ap.add_argument('--reps', type=int, default=0)
    ap.add_argument('--extra', default='', help='extra compiler flags (e.g. -mthumb -march=armv7-a -mfpu=neon -mfloat-abi=hard -static for the Pi)')
    ap.add_argument('--fused', help='with --replay: a harness object dir whose query_fusion.o (the fused guest query) is the reference')
    ap.add_argument('--native', default=str(ROOT / 'recomp/kernel/xk_native_4b9d0.c'))
    ap.add_argument('--keep', help='also write the test executable here (e.g. to copy it to another machine)')
    ap.add_argument('--build-only', action='store_true', help='compile differential cases for another machine; requires --keep (does not run tests)')
    ap.add_argument('--part', default='query', choices=['query', 'features'],
                    help='query: the BSP sphere query (f_00088110 subtree); features: the solver feature test (f_000864C0 subtree)')
    ap.add_argument('--replay-features', help='feature-test calls captured with XV_NATIVE_4B9D0_CAPTURE_FEATURES')
    a = ap.parse_args(); rec = Path(a.recomp)
    if a.build_only and (not a.keep or a.bench or a.replay or a.replay_features or a.mutants):
        ap.error('--build-only requires --keep and differential cases without replay, bench or mutants')
    env = dict(os.environ, N4_PART=a.part)
    texts = {}
    for p in sorted(rec.glob('code_*.c')):
        t = p.read_text(errors='replace')
        for fn in FUNCS:
            if fn not in texts and re.search(r'^void f_%s\(xctx' % fn, t, re.M): texts[fn] = t
    missing = [fn for fn in FUNCS if fn not in texts]
    if missing: sys.exit(f'not found in {rec}: {missing}')
    pre = texts['00088110']; preamble = pre[:pre.index('\nvoid f_')]
    guest = '#include "xv_x86rt.h"\n#include "xv_phase.h"\n' + preamble + '\n' + '\n'.join(body(texts[fn], fn) for fn in FUNCS)
    variants = {'plain -O2': ['-O2'], 'thread-table+render-view -O2': ['-O2', '-DXV_THREAD_PAGE_TABLE=1', '-DXV_RENDER_VIEW=1'],
                'plain -O0': ['-O0']}
    if a.variants != 'all':
        requested = set(a.variants.split(','))
        if requested - {k.split()[0] for k in variants}:
            ap.error('--variants must be all, plain, thread-table+render-view, or comma-separated names')
        variants = {k: v for k, v in variants.items() if k.split()[0] in requested}
    rc = 0
    with tempfile.TemporaryDirectory(prefix='xita-native-4b9d0-') as d:
        d = Path(d); (d / 'guest.c').write_text(guest); (d / 'kernel').mkdir()
        native_src = Path(a.native).read_text()
        def build(exe, flags, native_text):
            (d / 'kernel/xk_native_4b9d0.c').write_text(native_text)
            cmd = [a.cc, *flags, *a.extra.split(), "-std=gnu11", "-w", "-fno-strict-aliasing", "-ffp-contract=off", "-DXV_NATIVE_4B9D0=1", "-DXV_NATIVE_4B9D0_TEST=1", '-I' + str(rec),
                   '-I' + str(rec / 'kernel'), str(ROOT / 'tools/tests/native_4b9d0.c'), str(d / 'guest.c'),
                   str(d / 'kernel/xk_native_4b9d0.c'), '-lm', '-o', str(exe)]
            subprocess.run(cmd, check=True)
        if a.replay_features:
            exe = d / 'replay'; build(exe, variants.get('thread-table+render-view -O2', ['-O2']), native_src)
            if a.keep: subprocess.run(['cp', str(exe), a.keep], check=True)
            if a.cc != "cc" and "arm" in a.cc: print("built", exe); sys.exit(0)
            r = subprocess.run([str(exe), "0", "0", "--replay-features", a.replay_features, str(a.reps)], capture_output=True, text=True)
            print(r.stdout.strip() + r.stderr.strip()); sys.exit(r.returncode)
        if a.replay:
            fl = variants.get('thread-table+render-view -O2', ['-O2'])
            if a.fused:   # the game's fused query as the guest: a harness object directory (tools/host_build.py --out)
                o = Path(a.fused)
                fl = fl + ['-DN4_FUSED_GUEST=1'] + [str(o / n) for n in ('query_fusion.o', 'xk_collision_traversal_control.o',
                                                                         'xk_collision_vertices_control.o', 'xk_segment_sphere_control.o')] + ['-lpthread']
            exe = d / 'replay'; build(exe, fl, native_src)
            if a.keep: subprocess.run(['cp', str(exe), a.keep], check=True)
            if a.cc != "cc" and "arm" in a.cc: print("built", exe); sys.exit(0)   # cross build: run it on the target (--keep)
            r = subprocess.run([str(exe), "0", "0", "--replay", a.replay, str(a.reps)], capture_output=True, text=True)
            print(r.stdout.strip() + r.stderr.strip()); sys.exit(r.returncode)
        if a.bench:
            exe = d / 'bench'; build(exe, variants.get('thread-table+render-view -O2', ['-O2']), native_src)
            if a.keep: subprocess.run(['cp', str(exe), a.keep], check=True)
            r = subprocess.run([str(exe), a.cases, a.seed, '--bench', str(a.bench)], capture_output=True, text=True, env=env)
            print(r.stdout.strip() + r.stderr.strip()); sys.exit(r.returncode)
        for name, flags in variants.items():
            exe = d / 'test'; build(exe, flags, native_src)
            if a.keep: subprocess.run(['cp', str(exe), a.keep + '-' + name.split()[0]], check=True)
            if a.build_only:
                print(f'[{name}] built {a.keep}-{name.split()[0]}; NOT executed', flush=True)
                continue
            args = [str(exe), a.cases, a.seed] + (['--verify'] if a.verify else [])
            r = subprocess.run(args, capture_output=True, text=True, env=env)
            print(f'[{name}] ' + (r.stdout.strip() + r.stderr.strip()).replace('\n', f'\n[{name}] '), flush=True)
            rc |= r.returncode
        if a.mutants:
            caught = 0
            mutants = MUTANTS if a.part == 'query' else MUTANTS5
            only = os.environ.get('N4_MUTANT_ONLY')
            if only: mutants = [m for m in mutants if only in m[0]]
            for name, old, new in mutants:
                if native_src.count(old) != 1: print(f'[mutant] {name}: pattern not found once ({native_src.count(old)})'); rc |= 1; continue
                exe = d / 'mutant'; build(exe, ['-O2'], native_src.replace(old, new))
                try:
                    r = subprocess.run([str(exe), str(a.mutants), a.seed], capture_output=True, text=True, timeout=3600, env=env)
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
