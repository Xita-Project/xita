#!/usr/bin/env python3
"""Install --x87-regs function bodies into a stage's hand-maintained shards.

The stage shards (code_*.c) carry selective patches that a whole regeneration would lose (handoff §27), so
the register-stack lowering is spliced in function by function:

  1. regenerate twice with the same recompiler and flags, once without and once with --x87-regs:
       xita_recomp.py <xbe> --profile halo_ce_3925 --symbols <json> --phase-timing -o <base>
       xita_recomp.py <xbe> --profile halo_ce_3925 --symbols <json> --phase-timing --x87-regs -o <regs>
  2. tools/splice_x87_regs.py --stage <stage>/recomp --base <base> --regs <regs> [--dry-run]

A converted function (listed in <regs>/x87_regs_report.json) replaces the stage's copy only when the stage body
equals the baseline regeneration, or equals it after removing the stage's known observer patches (the entry
hook/phase-scope line order of older generators, tools/patch_scene_phase_timers.py call timers,
tools/patch_crt_float_hooks.py entry hooks). Those observers are re-installed into the new body at the same
places the patch tools put them. Any other difference keeps the stage body (reported as stage-differs).
Shards that receive a converted body get #include "xv_x87reg.h" (copy recomp/xv_x87reg.h into the stage).
Idempotent: a stage body already carrying the register lowering is left alone ("already").
"""
import argparse, glob, json, os, re, sys

FN = re.compile(r'^void f_([0-9A-F]{8})\(xctx \*restrict c\)\n\{\n', re.M)
TIMER = re.compile(r'^\s*\{ extern void xv_scene_phase_(begin|end)\(uint32_t\); xv_scene_phase_(begin|end)\(0x([0-9A-F]+)u\); \}\n', re.M)
CRT = re.compile(r'^#if defined\(XV_NATIVE_CRT_FLOAT\) && XV_NATIVE_CRT_FLOAT\n    \{ extern int xv_native_crt_float\(xctx \*, unsigned\); '
                 r'if \(xv_native_crt_float\(c, \d+\)\) return; \}[^\n]*\n#endif\n', re.M)
FK = '    uint32_t fk_a = 0, fk_b = 0, fk_r = 0; (void)fk_a; (void)fk_b; (void)fk_r;\n'
INCLUDE = '#include "xv_x87reg.h"   /* --x87-regs */\n'
MARK = '/* --x87-regs: x87 slots live in locals'


def functions(text):
    """name -> (start, end) of each 'void f_X(...) {...}' (end after the closing '}\\n')."""
    out = {}
    ms = list(FN.finditer(text))
    for i, m in enumerate(ms):
        limit = ms[i + 1].start() if i + 1 < len(ms) else len(text)
        k = text.find('\n}\n', m.start(), limit)
        if k < 0:
            raise SystemExit(f'f_{m.group(1)}: no closing brace')
        out[m.group(1)] = (m.start(), k + 3)
    return out


def load_dir(d):
    texts = {}
    where = {}
    for f in sorted(glob.glob(os.path.join(d, 'code_*.c'))):
        s = open(f).read()
        texts[f] = s
        for name, span in functions(s).items():
            where[name] = (f, span)
    return texts, where


def body(texts, where, name):
    f, (a, b) = where[name]
    return texts[f][a:b]


def split_prologue(text):
    """(header+prologue lines up to the fk_ line exclusive, rest). The prologue of an emitted function is
    everything between '{' and the fk_ declaration (locals, entry hooks, phase scope, x87 prologue, entry goto)."""
    i = text.index(FK)
    return text[:i], text[i:]


def normalize(text):
    """Stage-vs-regeneration comparison key: prologue lines as a sorted multiset, observers removed."""
    pro, rest = split_prologue(text)
    rest = CRT.sub('', rest)
    rest = TIMER.sub('', rest)
    lines = pro.split('\n')
    return '\n'.join(lines[:2] + sorted(lines[2:])) + '\x00' + rest


def timers(text):
    """Call sites wrapped by patch_scene_phase_timers in `text`: [(comment line, target, style)]."""
    out = []
    lines = text.split('\n')
    comment = None
    for i, line in enumerate(lines):
        if line.startswith('    /* ') and line.endswith(' */'):
            comment = line
            continue
        m = TIMER.match(line + '\n')
        if m and m.group(1) == 'begin':
            nxt = lines[i + 1] if i + 1 < len(lines) else ''
            style = 'push' if nxt.startswith('    X_PUSH32(') else 'any'
            out.append((comment, m.group(3), style, line))
    return out


def install_timers(text, sites):
    """Wrap the same calls in `text` the way the timer patch tool does (both lowering copies)."""
    if not sites:
        return text
    lines = text.split('\n')
    res = []
    i = 0
    wanted = {}
    for comment, target, style, begin in sites:
        wanted.setdefault(comment, []).append((target, style, begin))
    current = None
    while i < len(lines):
        line = lines[i]
        if line.startswith('    /* ') and line.endswith(' */'):
            current = wanted.get(line)
        if current:
            for target, style, begin in current:
                call = f'    f_{target}(c);'
                if style == 'push' and line.startswith('    X_PUSH32(') and i + 1 < len(lines) and lines[i + 1] == call:
                    end = begin.replace('xv_scene_phase_begin', 'xv_scene_phase_end')
                    res += [begin, line, call, end]; i += 2; break
                if style == 'any' and line == call:
                    end = begin.replace('xv_scene_phase_begin', 'xv_scene_phase_end')
                    res += [begin, line, end]; i += 1; break
            else:
                res.append(line); i += 1
            continue
        res.append(line); i += 1
    return '\n'.join(res)


def rebuild(stage_fn, base_fn, regs_fn):
    """The register body with the stage's observers; None when the stage differs otherwise."""
    if stage_fn == base_fn:
        return regs_fn
    if normalize(stage_fn) != normalize(base_fn):
        return None
    s_pro, s_rest = split_prologue(stage_fn)
    b_pro, _ = split_prologue(base_fn)
    r_pro, r_rest = split_prologue(regs_fn)
    b_lines = set(b_pro.split('\n'))
    extra = [line for line in r_pro.split('\n') if line not in b_lines]      # the x87 prologue lines
    goto = [line for line in s_pro.split('\n') if line.startswith('    goto L_')]
    s_lines = [line for line in s_pro.split('\n') if not line.startswith('    goto L_')]
    if s_lines and s_lines[-1] == '':
        s_lines = s_lines[:-1]
    pro = '\n'.join(s_lines + [line for line in extra if line] + goto) + '\n'
    crt = CRT.search(s_rest)
    if crt and s_rest.startswith(FK) and s_rest[len(FK):].startswith(crt.group(0)):
        r_rest = r_rest.replace(FK, FK + crt.group(0), 1)
    elif crt:
        return None
    out = install_timers(pro + r_rest, timers(stage_fn))
    # the result may differ from the plain register body only by the stage's observers
    if normalize(out) != normalize(regs_fn):
        return None
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--stage', required=True, help='<stage>/recomp with the maintained code_*.c')
    ap.add_argument('--base', required=True, help='baseline regeneration (same recompiler, no --x87-regs)')
    ap.add_argument('--regs', required=True, help='--x87-regs regeneration (holds x87_regs_report.json)')
    ap.add_argument('--only', default=None, help='splice only these entries (hex, comma separated)')
    ap.add_argument('--exclude', default=None, help='never splice these entries (hex, comma separated)')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--report', default=None, help='write a JSON splice report here')
    a = ap.parse_args()
    rep = json.load(open(os.path.join(a.regs, 'x87_regs_report.json')))
    todo = sorted(int(k, 16) for k in rep['converted_functions'])
    if a.only:
        only = {int(x, 16) for x in a.only.split(',') if x}
        todo = [e for e in todo if e in only]
    if a.exclude:
        ex = {int(x, 16) for x in a.exclude.split(',') if x}
        todo = [e for e in todo if e not in ex]
    st_texts, st_where = load_dir(a.stage)
    b_texts, b_where = load_dir(a.base)
    r_texts, r_where = load_dir(a.regs)
    result = {}
    edits = {}
    for e in todo:
        name = f'{e:08X}'
        if name not in st_where or name not in b_where or name not in r_where:
            result[name] = 'missing'; continue
        s = body(st_texts, st_where, name)
        if MARK in s:
            result[name] = 'already'; continue
        new = rebuild(s, body(b_texts, b_where, name), body(r_texts, r_where, name))
        if new is None:
            result[name] = 'stage-differs'; continue
        f, span = st_where[name]
        edits.setdefault(f, []).append((span, new))
        result[name] = 'spliced' if s == body(b_texts, b_where, name) else 'spliced-with-observers'
    from collections import Counter
    counts = Counter(result.values())
    print(f'x87-regs splice: {dict(counts)} ({len(edits)} shards touched){" [dry run]" if a.dry_run else ""}')
    for name, r in sorted(result.items()):
        if r in ('stage-differs', 'missing'):
            print(f'  f_{name}: {r}')
    if a.report:
        json.dump({'counts': dict(counts), 'functions': result}, open(a.report, 'w'), indent=1)
    if a.dry_run:
        return 0
    for f, items in edits.items():
        s = st_texts[f]
        for (lo, hi), new in sorted(items, key=lambda x: -x[0][0]):
            s = s[:lo] + new + s[hi:]
        if INCLUDE not in s:
            anchor = '#include "xv_recomp_protos.h"\n'     # first one (a few shards include it again later)
            if anchor not in s or s.index(anchor) > s.index('\nvoid f_'):
                raise SystemExit(f'{f}: include anchor drift')
            s = s.replace(anchor, anchor + INCLUDE, 1)
        with open(f, 'w') as fh:
            fh.write(s)
    return 0


if __name__ == '__main__':
    sys.exit(main())
