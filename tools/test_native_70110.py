#!/usr/bin/env python3
"""Differential test of the native material setup (recomp/kernel/xk_native_70110.c) against the lifted guest body of a
stage (generated code is not in the repository).

  tools/test_native_70110.py <stage>/recomp [cases] [--seed N] [--cc CC] [--variants a,b] [--mutants] [--no-verify]
                             [--bench N] [--threads N --iters K] [--keep FILE --cflags ...]

Extracts f_00070110 and the shard's preamble, patches it like the stage (tools/patch_native_70110_hooks.py: the wrapper,
the renamed body, the tapped copy), links it with tools/tests/native_70110.c (deterministic stand-ins for every callee,
HLE and memo) and the native, and runs the randomized cases in several builds: plain, with the lift's optional hooks
(XV_MODEL_UV, XV_MODEL_FOG, XV_NATIVE_MATERIAL_SAMPLER), with the per-thread page table and the render view, with object
jobs, at -O1 (the host harness) and -O0. --mutants builds deliberately broken copies of the native and expects each to
be caught."""
import argparse, os, re, subprocess, sys, tempfile, importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('p70', ROOT / 'tools/patch_native_70110_hooks.py'); P = importlib.util.module_from_spec(spec); spec.loader.exec_module(P)
HOOKS = ['-DXV_MODEL_UV=1', '-DXV_MODEL_FOG=1', '-DXV_NATIVE_MATERIAL_SAMPLER']
VARIANTS = {
    'plain -O2': ['-O2'],
    'hooks -O2': ['-O2', *HOOKS],
    'hooks+thread-table+render-view -O2': ['-O2', *HOOKS, '-DXV_THREAD_PAGE_TABLE=1', '-DXV_RENDER_VIEW=1'],
    'hooks+thread-table -O2': ['-O2', *HOOKS, '-DXV_THREAD_PAGE_TABLE=1'],
    'hooks+object-jobs -O2': ['-O2', *HOOKS, '-DXV_EXPERIMENTAL_OBJECT_JOBS'],
    'hooks -O1': ['-O1', *HOOKS],
    'hooks -O0': ['-O0', *HOOKS],
}
# (description, old, new[, flags]): old must match exactly once in the native (flag 'a': every occurrence; 'v': caught
# by verify mode, run with it); the test must report mismatches (or crash). Equivalent ones are not listed: the order
# of a faddp's operands (exact in IEEE but for NaN payloads), the FAST esp re-derivation (the check already holds), eax
# not reloaded after SetVertexShaderConstant (eax is written again before any read or store after all four), movsx at
# 70662 as movzx (ax is 1..4 there).
MUTANTS = [
    ('fsw: the TOP of a compare not OR-ed in', '(((T0 + (off)) & 7u) << 11)', '0u'),
    ('x87 top after a float-returning callee', 'T0 = (c->fsp + (uint32_t)(d)) & 7u;', 'T0 = c->fsp & 7u;'),
    ('ecx never stored', '#define ST_ecx c->r[1] = ecx;', '#define ST_ecx'),
    ('the stale carry cell never stored', '#define ST_fcf c->f_cf = fcf;', '#define ST_fcf'),
    ('x87 slot 6 never stored', '#define ST_x6 c->st[(T0 + 6u) & 7u] = xs[6];', '#define ST_x6'),
    ('fsw never stored', '#define ST_fsw c->fsw = fsw;', '#define ST_fsw'),
    ('back-edge budget not charged', 'if (--c->preempt <= 0)', 'if (c->preempt <= 0)'),
    ('sampler group: outcome inverted', 'h__ = xv_material_sampler_try(c, (g));', 'h__ = !xv_material_sampler_try(c, (g));'),
    ('fog memo: a hit not taken', 'if (h__) goto target;', 'if (0) goto target;'),
    ('uv memo: token not passed', 'xk_model_uv_end(c, e->utok);', 'xk_model_uv_end(c, 0);'),
    ('FAST frame base off by 4', '#define N70_SP(a) (FAST ? w0 + ((uint32_t)(a) - (E - N70_WLO))', '#define N70_SP(a) (FAST ? w0 + 4 + ((uint32_t)(a) - (E - N70_WLO))'),
    ('image constant 0.0 read as 1.0 (prologue compare)', '    /* 00070164  fcomp dword ptr ds:[1F0A68h] */\n    FCOMT(7, XS(7), KLDF(0x1F0A68u));', '    /* 00070164  fcomp dword ptr ds:[1F0A68h] */\n    FCOMT(7, XS(7), KLDF(0x1F0A78u));'),
    ('sbb without the carry of neg', 'uint32_t cf_ = FC; uint32_t r_ = (uint32_t)(a_ - b_ - cf_); FLAGS_C(XK_SBB', 'uint32_t cf_ = 0; uint32_t r_ = (uint32_t)(a_ - b_ - cf_); FLAGS_C(XK_SBB'),
    ('shr bl,3 by 2', 'SHR8(R8L(ebx), 0x3u)', 'SHR8(R8L(ebx), 0x2u)'),
    ('shr cl,4 without its flag cells', 'SET8L(ecx, SHR8(R8L(ecx), 0x4u));', 'SET8L(ecx, R8L(ecx) >> 4);'),
    ('render-state shadow 18F474 not written', 'KW32(0x18F474u, 0x7Fu);', ''),
    ('render-state shadow 18F480 from edx', 'KW32(0x18F480u, edi);', 'KW32(0x18F480u, edx);'),
    ('sete cl inverted', 'SET8L(ecx, FZ ? 1 : 0);', 'SET8L(ecx, FZ ? 0 : 1);'),
    ('test ah,5 / jp tested as ZF', "if (n70_ccj(cc, 0x5u, 'P')) goto L_000702F6;", "if (n70_ccj(cc, 0x5u, 'Z')) goto L_000702F6;"),
    ('fxch at 70292 missing', '    /* 00070292  fxch */\n    { double t_ = XS(5); XS(5) = XS(6); XS(6) = t_; } XD(5); XD(6);', '    /* 00070292  fxch */'),
    ('fsubr at 70B0E as fsub', 'XS(5) = KLDF(0x2FC8D8u) - XS(5); XD(5);', 'XS(5) = XS(5) - KLDF(0x2FC8D8u); XD(5);', 'a'),
    ('light index scaled by 4 instead of 3 (70665)', '    /* 00070665  lea edx,[eax+eax*2] */\n    edx = (eax+(eax*2));', '    /* 00070665  lea edx,[eax+eax*2] */\n    edx = (eax+(eax*4));'),
    ('stack word [esp+10h] = 1.0 missing (70305)', '    /* 00070303  fstp st(0) */\n    /* st(0) = st(0) */\n    /* pop */\n    /* 00070305  mov dword ptr [esp+10h],3F800000h */\n    SW32((esp+0x10u), 0x3F800000u);', '    /* 00070303  fstp st(0) */\n    /* st(0) = st(0) */\n    /* pop */\n    /* 00070305  mov dword ptr [esp+10h],3F800000h */'),
    ('cull mode pushed as 900h (7036D)', '    /* 0007036D  push 901h */\n    PUSH(0x901u);', '    /* 0007036D  push 901h */\n    PUSH(0x900u);'),
    ('material across a page end read from one page', 'if ((ebp & 0xFFFu) + N70_MLEN > 0x1000u) return N70_BAIL + 0; ', ''),
    ('texture of stage 0 from [ebp+E8h]', 'ecx = MB32(0xB0u);', 'ecx = MB32(0xE8u);'),
    ('fstp [esp+B0h] missing', 'SSTF((esp+0xB0u), XS(7));', ''),
    ('dirty list of the first texture bind without eax', 'CALL(15, f_00080360, 0xb0, 0, 0, (ST_eax ST_ecx', 'CALL(15, f_00080360, 0xb0, 0, 0, (ST_ecx'),
    ('HLE: ecx stored for mode 2 only after the call', 'SYNCL(hle); n70_hle_call(c, addr, xv_hle_##name, "xv_hle_" #name);', 'n70_hle_call(c, addr, xv_hle_##name, "xv_hle_" #name); SYNCL(hle);'),
    ('verify: the dry run does not write its frame shadow', 'if (o < N70_WSIZE) { e->shadow[o] = s[i]; inwin++; }', 'if (o < N70_WSIZE) { inwin++; }', 'v'),
    ('verify: the dry run stops after an HLE instead of before', 'if (DRY) { SYNC_AT(d, full); return k; }', 'if (DRY) { SYNC_AT(d, full); n70_hle_call(c, addr, xv_hle_##name, "xv_hle_" #name); return k; }', 'v'),
]

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('recomp'); ap.add_argument('cases', nargs='?', default='2000')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc')); ap.add_argument('--seed', default='1')
    ap.add_argument('--variants', default=''); ap.add_argument('--mutants', action='store_true'); ap.add_argument('--no-verify', action='store_true')
    ap.add_argument('--native', default=str(ROOT / 'recomp/kernel/xk_native_70110.c'))
    ap.add_argument('--mutant-filter', default='')
    ap.add_argument('--bench', type=int, default=0, help='time guest and native on game-like scenes (repetitions)')
    ap.add_argument('--threads', type=int, default=0); ap.add_argument('--iters', type=int, default=20)
    ap.add_argument('--replay', default='', help='entry states captured in the game (XV_NATIVE_70110_CAPTURE)')
    ap.add_argument('--reps', type=int, default=0, help='--replay: timing repetitions')
    ap.add_argument('--keep', default='', help='copy the (first variant) test binary here and do not run it (cross builds: --cc, --cflags)')
    ap.add_argument('--cflags', default='')
    ap.add_argument('--guest-cflags', default='', help='extra flags for the lifted body only (e.g. -Os: the Vita builds code_011 -Os)')
    ap.add_argument('--native-cflags', default='', help='extra flags for the native only')
    a = ap.parse_args(); rec = Path(a.recomp).resolve()
    src = next((p.read_text(errors='replace') for p in sorted(rec.glob('code_*.c')) if re.search(r'^void f_00070110\(xctx', p.read_text(errors='replace'), re.M)), None)
    if src is None: sys.exit('no shard defines f_00070110')
    m = re.search(r'^void f_00070110\(xctx \*restrict c\)\n\{\n', src, re.M)
    body = src[m.start():src.index('\n}\n', m.end()) + 3]
    if 'xv_native_70110' in src[m.start() - 2000:m.start()]:
        pass   # a patched stage: the body is the translated one either way
    preamble = src[:src.index('\nvoid f_')]
    vbody, nsites = P.tapped(body)
    guest = ('#include "xv_x86rt.h"\n#include "xv_phase.h"\n#ifndef XV_EXPERIMENTAL_OBJECT_JOBS\n#define XV_HLE_PROXY(fn) 0   /* the protos define it only with object jobs */\n#endif\n'
             + preamble + '\n' + P.PRE + body + P.VPRE + vbody + P.VPOST)
    kernel = rec / 'kernel'
    variants = {k: v for k, v in VARIANTS.items() if not a.variants or any(x in k for x in a.variants.split(','))}
    native_src = Path(a.native).read_text()
    rc = 0
    with tempfile.TemporaryDirectory(prefix='xita-native-70110-') as d:
        d = Path(d); (d / 'guest.c').write_text(guest)
        def build(flags, native_path, exe):
            common = [*flags, *a.cflags.split(), '-std=gnu11', '-w', '-fno-strict-aliasing', '-ffp-contract=off', '-DXV_NATIVE_70110=1',
                      '-I' + str(rec), '-I' + str(kernel)]
            objs = []
            for src, extra_flags in ((ROOT / 'tools/tests/native_70110.c', []), (d / 'guest.c', a.guest_cflags.split()), (native_path, a.native_cflags.split())):
                o = d / (Path(src).stem + '.o'); subprocess.run([a.cc, *common, *extra_flags, '-c', str(src), '-o', str(o)], check=True); objs.append(str(o))
            subprocess.run([a.cc, *a.cflags.split(), *objs, '-lm', '-lpthread', '-o', str(exe)], check=True)
        def run(exe, cases, seed, extra=()):
            r = subprocess.run([str(exe), str(cases), str(seed), *extra], capture_output=True, text=True)
            return r.returncode, (r.stdout + r.stderr).strip()
        (d / 'native.c').write_text(native_src)
        extra = ['--no-verify'] if a.no_verify else []
        if a.bench: extra = ['--bench', str(a.bench)]
        if a.threads: extra = ['--threads', str(a.threads), str(a.iters)]
        if a.replay: extra = ['--replay', str(Path(a.replay).resolve()), str(a.reps)]
        print(f'guest body: {nsites} sites tapped', flush=True)
        for name, flags in variants.items():
            exe = d / 'test'; build(flags, d / 'native.c', exe)
            if a.keep:
                import shutil; shutil.copy(exe, a.keep); print(f'[{name}] built {a.keep}'); break
            code, out = run(exe, a.cases, a.seed, extra)
            print(f'[{name}] ' + out.replace('\n', f'\n[{name}] '), flush=True)
            rc |= code
        if a.mutants:
            caught = 0
            mutants = [x for x in MUTANTS if a.mutant_filter in x[0]]
            for desc, old, new, *fl in mutants:
                fl = fl[0] if fl else ''
                cnt = native_src.count(old)
                if cnt == 0 or (cnt != 1 and 'a' not in fl): print(f'[mutant] {desc}: pattern matched {cnt} times'); rc |= 1; continue
                (d / 'mutant.c').write_text(native_src.replace(old, new))
                exe = d / 'mutant'
                build(['-O2', *HOOKS], d / 'mutant.c', exe)
                code, out = run(exe, a.cases, a.seed, [] if 'v' in fl else ['--no-verify'])
                mm = re.search(r'(\d+) mismatches, verify (\d+)', out)
                n = (int(mm.group(1)) + int(mm.group(2))) if mm else -1
                crashed = code not in (0, 1)
                caught += n > 0 or crashed
                print(f'[mutant] {desc}: {n} of {a.cases} cases mismatch' + (' (crashed)' if crashed else '') + ('' if n > 0 or crashed else '  <-- NOT CAUGHT'), flush=True)
            print(f'[mutant] {caught}/{len(mutants)} caught')
            if caught != len(mutants): rc |= 1
    sys.exit(rc)

if __name__ == '__main__': main()
