#!/usr/bin/env python3
"""Install the XV_NATIVE_70110 hook into a stage's shards (idempotent).
Usage: patch_native_70110_hooks.py <stage>/recomp

f_00070110 (the material setup, `ret 1Ch`) becomes a wrapper in front of its translated body, which is renamed
f_00070110_body, and a copy of the body with verify taps, f_00070110_vbody, follows it:

    #if defined(XV_NATIVE_70110) && XV_NATIVE_70110
    void f_00070110_body(xctx *restrict c);
    void (f_00070110)(xctx *restrict c) { ... if (!xv_native_70110(c, <cfg>)) f_00070110_body(c); }
    #define f_00070110 f_00070110_body
    #endif
    void f_00070110(xctx *restrict c) { ... }       <- the translated body, compiled as f_00070110_body under the flag
    #if defined(XV_NATIVE_70110) && XV_NATIVE_70110
    #undef f_00070110
    void f_00070110_vbody(xctx *restrict c, void *xv_n70_vs) { ... }   <- the same body with taps
    #endif

<cfg> tells recomp/kernel/xk_native_70110.c which of the lift's optional hooks this shard was compiled with
(XV_MODEL_UV 1, XV_MODEL_FOG 2, XV_NATIVE_MATERIAL_SAMPLER 4). The copy calls xv_native_70110_tap(c, vs, kind, key,
post, outcome) before (post 0) and after (post 1) every site the native numbers: translated callees (kind 1, key: the
pushed return address), D3D HLE calls (2), the f_00056F20 unit with the XV_MODEL_UV memo (3), the XV_MODEL_FOG memo's
begin (4, 0x70A42) and end (5, 0x70D07), the back-edge 70321 -> 7030D (6, 0x70321) and the XV_NATIVE_MATERIAL_SAMPLER
groups (7, the group; through xv_native_70110_sampler). Only verify mode (XV_NATIVE_70110=1) runs the copy. Without the
build flag the shard is unchanged in effect (the body keeps its name)."""
import re, sys, glob
FN = '00070110'
PRE = ('#if defined(XV_NATIVE_70110) && XV_NATIVE_70110\n'
       '/* recomp/kernel/xk_native_70110.c: the native material setup in front of the translated body (renamed f_00070110_body),\n'
       ' * which runs when the native is off or declines; f_00070110_vbody below is the body with verify taps. */\n'
       '#if defined(XV_MODEL_UV) && XV_MODEL_UV\n#define XV_N70_CFG_UV 1u\n#else\n#define XV_N70_CFG_UV 0u\n#endif\n'
       '#if defined(XV_MODEL_FOG) && XV_MODEL_FOG\n#define XV_N70_CFG_FOG 2u\n#else\n#define XV_N70_CFG_FOG 0u\n#endif\n'
       '#ifdef XV_NATIVE_MATERIAL_SAMPLER\n#define XV_N70_CFG_SAMPLER 4u\n#else\n#define XV_N70_CFG_SAMPLER 0u\n#endif\n'
       'void f_00070110_body(xctx *restrict c);\n'
       'void (f_00070110)(xctx *restrict c) { extern int xv_native_70110(xctx *, unsigned);   /* (name in parentheses: the\n'
       '     Makefile counts the definitions matching ^void f_00070110) */\n'
       '    if (!xv_native_70110(c, XV_N70_CFG_UV | XV_N70_CFG_FOG | XV_N70_CFG_SAMPLER)) f_00070110_body(c); }\n'
       '#define f_00070110 f_00070110_body\n'
       '#endif\n')
VPRE = ('#if defined(XV_NATIVE_70110) && XV_NATIVE_70110\n'
        '#undef f_00070110\n'
        '/* verify mode only (recomp/kernel/xk_native_70110.c): the body with a tap before and after every site */\n'
        'void xv_native_70110_tap(xctx *, void *, unsigned, uint32_t, int, int);\n'
        'int xv_native_70110_sampler(xctx *, void *, unsigned);\n'
        '#define XV_N70_T(kind, key, post) xv_native_70110_tap(c, xv_n70_vs, (kind), (key), (post), 0)\n')
VPOST = ('#undef XV_N70_T\n'
         '#endif\n')
MARK = '/* recomp/kernel/xk_native_70110.c: the native material setup'

def tapped(body):
    """the body text with taps (f_00070110_vbody)"""
    L = body.split('\n')
    assert L[0] == 'void f_00070110(xctx *restrict c)', L[0]
    L[0] = 'void f_00070110_vbody(xctx *restrict c, void *xv_n70_vs)'
    # the Makefile counts these markers in the shards (XV_MODEL_UV/FOG, XV_NATIVE_MATERIAL_SAMPLER): not in the copy
    L = [l.replace('XV_MODEL_UV_CALLS:', 'tapped copy of the model UV call sites,').replace('XV_MODEL_FOG_SCOPE:', 'tapped copy of the model fog scope,')
          .replace('XV_MATERIAL_SAMPLER_GROUP', 'tapped copy of material sampler group') for l in L]
    out, key, i, uv_open, sites = [], None, 0, None, 0
    while i < len(L):
        l = L[i]; s = l.strip()
        m = re.match(r'X_PUSH32\((0x[0-9A-F]+u)\);$', s)
        if m: key = m.group(1)
        if s == '#if defined(XV_MODEL_UV) && XV_MODEL_UV' and i + 1 < len(L) and L[i + 1].strip().startswith('if (!xk_model_uv_begin('):
            out.append(f'    XV_N70_T(3, {key}, 0);'); out.append(l); uv_open = key; sites += 1; i += 1; continue
        if uv_open and s == '#endif' and L[i - 1].strip() == '}' and L[i - 2].strip().startswith('xk_model_uv_end('):
            out.append(l); out.append(f'    XV_N70_T(3, {uv_open}, 1);'); uv_open = None; i += 1; continue
        if re.match(r'f_[0-9A-F]{8}\(c\);$', s) and not uv_open:
            out += [f'    XV_N70_T(1, {key}, 0);', l, f'    XV_N70_T(1, {key}, 1);']; sites += 1; i += 1; continue
        if s.startswith('XV_HLE_CALL('):
            out += [f'    XV_N70_T(2, {key}, 0);', l, f'    XV_N70_T(2, {key}, 1);']; sites += 1; i += 1; continue
        mf = re.match(r'if \((xk_model_fog_begin\(c, xram_, xpt_, imgb_, &xv_model_fog_token_\))\) goto (L_[0-9A-F]{8});$', s)
        if mf:
            out.append(f'    {{ XV_N70_T(4, 0x70A42u, 0); int xv_n70_h = {mf.group(1)}; '
                       f'xv_native_70110_tap(c, xv_n70_vs, 4, 0x70A42u, 1, xv_n70_h); if (xv_n70_h) goto {mf.group(2)}; }}')
            sites += 1; i += 1; continue
        if s.startswith('xk_model_fog_end('):
            out += ['    XV_N70_T(5, 0x70D07u, 0);', l, '    XV_N70_T(5, 0x70D07u, 1);']; sites += 1; i += 1; continue
        ms = re.match(r'(\s*)if \(!xv_material_sampler_try\(c, (\d)u\)\) \{$', l)
        if ms:
            out.append(f'{ms.group(1)}if (!xv_native_70110_sampler(c, xv_n70_vs, {ms.group(2)}u)) {{'); sites += 1; i += 1; continue
        if s == 'X_PREEMPT();':
            out += ['    XV_N70_T(6, 0x70321u, 0);', l, '    XV_N70_T(6, 0x70321u, 1);']; sites += 1; i += 1; continue
        out.append(l); i += 1
    assert uv_open is None
    return '\n'.join(out), sites

def main():
    root = sys.argv[1]; found = 0
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read()
        m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % FN, s, re.M)
        if not m: continue
        found += 1
        end = s.index('\n}\n', m.end()) + 3
        body = s[m.start():end]
        head, tail = s[:m.start()], s[end:]
        if not head.endswith(PRE) and MARK in head[-3000:]:          # an older wrapper: replace it
            k = head.rindex('#if defined(XV_NATIVE_70110) && XV_NATIVE_70110\n', 0, head.rindex(MARK)); head = head[:k]
        if head.endswith(PRE) and tail.startswith(VPRE):
            print(f'{f}: f_{FN} wrapper and tapped copy already present'); continue
        assert 'xv_native_70110' not in body
        vbody, n = tapped(body)
        if not head.endswith(PRE): head += PRE
        if tail.startswith('#if defined(XV_NATIVE_70110) && XV_NATIVE_70110\n#undef f_00070110\n'):   # an older copy: replace it
            k = tail.index(VPOST) + len(VPOST); tail = tail[k:]
        s = head + body + VPRE + vbody + VPOST + tail
        open(f + '.tmp', 'w').write(s)
        import os; os.replace(f + '.tmp', f)   # a new file: never write through a hard link into another stage
        print(f'{f}: wrapper installed in front of f_{FN}, tapped copy with {n} sites')
    if found != 1: sys.exit(f'expected exactly one definition of f_{FN}, found {found}')
if __name__ == '__main__': main()
