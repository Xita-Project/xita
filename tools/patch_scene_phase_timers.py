#!/usr/bin/env python3
"""Install the XV_SCENE_PHASES timers around the direct guest calls of the scene entry f_000BCB30 and of its main callee
f_0005DBC0 in a stage's hand-maintained shards (idempotent: earlier timers are stripped and re-installed).
Usage: patch_scene_phase_timers.py <stage>/recomp [--parents A,B] [--hle-calls] [--any-call --tail-calls] [--native-object-collect] [--collision-hooks]
Tail-call timing prevents compiler tail-call elimination; use only for diagnostics.
Report: [scene-phases] in recomp/kernel/xk_scene_thread.c."""
import re, sys, glob
from pathlib import Path
from collections import Counter
ANY_CALL = False
PARENTS = ['000900E0', '0014A162', '000B9678', '000B8840',   # the sim tick's two big callees (38 + 14 ms/frame, perf125) and the Pi sampler's hot owner chain (B9678 -> B8980 66%)
           '00109760', '0005B4A0', '00062240', '000A26B0',   # sim tick (2-3/frame at 12 fps), per-model chain, the 182/frame callee (perf124 split)
           '000FA920',   # the tick root (54 ms/frame in the a10 cinematic, perf122): its direct callees are the tick's phases
           '000BD420',   # the owner's frame loop (tick side): its direct callees are the tick phases ([tick-phases])
           '000BCB30', '0005DBC0', '0005D990', '0005C5E0', '0005D410', '0005BCB0', '00028320',
           '000606B0', '00054010', '00060560', '0005B760', '00054740', '0005B710', '000539C0', '00092890', '00093C00']   # + the Vita's top scene callees (§47), one level down   # scene entry, its main callee, and the chain below (each level was one callee on the host)
def wrap_hle(body):
    """Time the existing macro invocation, retaining proxy/dispatch semantics."""
    count = 0
    def wrap(m):
        nonlocal count
        count += 1
        indent, addr = m.group(1), m.group(2)
        return (indent + '{ extern void xv_scene_phase_begin(uint32_t); xv_scene_phase_begin(0x%su); }\n' % addr
                + m.group(0)
                + indent + '{ extern void xv_scene_phase_end(uint32_t); xv_scene_phase_end(0x%su); }\n' % addr)
    body = re.sub(r'^([ \t]*)XV_HLE_CALL\(0x([0-9A-Fa-f]+)u,\s*\w+\);\n', wrap, body, flags=re.M)
    return body, count

def report_unwrapped(body, parent):
    calls = Counter(re.findall(r'^\s*f_([0-9A-F]{8})\(c\);', body, re.M))
    wrapped = Counter(a.upper().zfill(8) for a in re.findall(r'xv_scene_phase_begin\(0x([0-9A-Fa-f]+)u\)', body))
    missing = calls - wrapped
    if missing:
        print('WARNING f_' + parent + ': unwrapped guest calls ' +
              ', '.join(f'{a} x{n}' for a, n in sorted(missing.items())) +
              '; use --any-call for conditional-hook callers')

BEGIN = '    { extern void xv_scene_phase_begin(uint32_t); xv_scene_phase_begin(0x%su); }\n'

def native_collect_patch(root):
    """Time the real callback that the native walk bypasses in generated code.

    Prepare before editing shards so an unknown native layout fails closed.
    Keep this opt-in: ordinary production builds need no extra observer calls.
    """
    path = Path(root) / 'kernel/xk_object_collect.c'
    original = path.read_text()
    call = 'else f_001716F0(c);'
    wrapped = ('else { /* XV_NATIVE_COLLECT_PHASE */\n'
               '                        extern void xv_scene_phase_begin(uint32_t);\n'
               '                        extern void xv_scene_phase_end(uint32_t);\n'
               '                        xv_scene_phase_begin(0x1716F0u);\n'
               '                        f_001716F0(c);\n'
               '                        xv_scene_phase_end(0x1716F0u);\n'
               '                    }')
    if original.count(wrapped) == 1 and call not in original:
        return path, original
    if original.count(call) != 1 or 'XV_NATIVE_COLLECT_PHASE' in original:
        raise SystemExit('native object collector layout changed; no timers installed')
    return path, original.replace(call, wrapped)

def collision_hooks(source):
    """Observe specialized calls under the same guest addresses as fallbacks.

    These are whole-call scopes, including native dispatch/reuse. They do not
    claim to split the internal fused query or solver into individual leaves.
    """
    source = re.sub(r'^.*?/\* XV_COLLISION_PHASE \*/\n', '', source, flags=re.M)
    targets = {
        'nq_collection_172c95(c);': '00171F10',
        'ns_solver_at_172cb8(c,0x172cb8u);': '00170C10',
        'nq_query_at_171f94(c,0x172c95u);': '00088110',
        'nq_query_at_17301b(c);': '00088110',
    }
    # The specialized collector is a separate static function and therefore is
    # not covered by --parents 00171F10. Include its ordinary direct children.
    start = source.find('static void nq_collection_172c95(xctx *restrict c)\n{')
    body_start = source.find('{', start) + 1 if start >= 0 else -1
    def wrap(m):
        indent, call = m.group(1), m.group(2)
        addr = targets.get(call)
        if addr is None:
            if start < 0 or m.start() < start:
                return m.group(0)
            # Restrict to this function; do not accidentally time a later one.
            if re.search(r'^\S.*\([^\n]*\)\n\{', source[body_start:m.start()], re.M):
                return m.group(0)
            addr = re.fullmatch(r'f_([0-9A-F]{8})\(c\);', call).group(1)
        return (indent + '{ extern void xv_scene_phase_begin(uint32_t); xv_scene_phase_begin(0x' + addr + 'u); } /* XV_COLLISION_PHASE */\n'
                + m.group(0)
                + indent + '{ extern void xv_scene_phase_end(uint32_t); xv_scene_phase_end(0x' + addr + 'u); } /* XV_COLLISION_PHASE */\n')
    pattern = '|'.join(re.escape(k) for k in targets)
    return re.sub(r'^([ \t]*)(' + pattern + r'|f_[0-9A-F]{8}\(c\);)\n', wrap, source, flags=re.M)

def main():
    root = sys.argv[1]; total = 0
    native = native_collect_patch(root) if '--native-object-collect' in sys.argv else None
    global PARENTS
    global ANY_CALL
    ANY_CALL = '--any-call' in sys.argv   # wrap every bare f_XXXXXXXX(c); line, not only push+call pairs
    if '--tail-calls' in sys.argv and not ANY_CALL:
        raise SystemExit('--tail-calls requires --any-call')
    if '--parents' in sys.argv:   # e.g. --parents 000BD420,000FA920: only these loops (a few calls/frame, no measurable cost)
        PARENTS = sys.argv[sys.argv.index('--parents') + 1].split(',')
    for f in glob.glob(root + '/code_*.c'):
        s = open(f).read(); changed = False
        for fn in PARENTS:
            m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, s, re.M)
            if not m: continue
            next_function = re.search(r'^\S[^\n]*\([^\n]*\)\n\{', s[m.end():], re.M)
            end = m.end() + next_function.start() if next_function else len(s)
            body = s[m.end():end]
            body = re.sub(r'^\s*\{ extern void xv_scene_phase_(begin|end)\([^\n]*\n', '', body, flags=re.M)   # strip old (any indentation)
            n = [0]
            if ANY_CALL:   # every bare `f_XXXXXXXX(c);` line (calls wrapped by object-jobs/#if code have no push right before them)
                if '--tail-calls' in sys.argv:
                    # Preserve the return after the end observer. Do not match
                    # conditional calls or statements with trailing guest work.
                    body = re.sub(r'^([ \t]*)f_([0-9A-F]{8})\(c\);[ \t]*return;[ \t]*$',
                                  r'\1f_\2(c);\n\1return;', body, flags=re.M)
                def wrap_any(mm):
                    n[0] += 1; ind, a = mm.group(1), mm.group(2)
                    return (ind + '{ extern void xv_scene_phase_begin(uint32_t); xv_scene_phase_begin(0x%su); }\n' % a + ind + 'f_%s(c);\n' % a
                            + ind + '{ extern void xv_scene_phase_end(uint32_t); xv_scene_phase_end(0x%su); }\n' % a)
                body = re.sub(r'^([ \t]*)f_([0-9A-F]{8})\(c\);\n', wrap_any, body, flags=re.M)
                if '--hle-calls' in sys.argv:
                    body, hle_count = wrap_hle(body); n[0] += hle_count
                report_unwrapped(body, fn)
                s = s[:m.end()] + body + s[end:]; changed = True; total += n[0]
                print(f'f_{fn}: {n[0]} call sites (any-call) in {f}')
                continue
            def wrap(mm):
                n[0] += 1
                return (BEGIN % mm.group(2)) + mm.group(1) + '    { extern void xv_scene_phase_end(uint32_t); xv_scene_phase_end(0x%su); }\n' % mm.group(2)
            body = re.sub(r'(    X_PUSH32\(0x[0-9A-Fa-f]+u\);\n    f_([0-9A-F]{8})\(c\);\n)', wrap, body)
            if '--hle-calls' in sys.argv:
                body, hle_count = wrap_hle(body); n[0] += hle_count
            report_unwrapped(body, fn)
            s = s[:m.end()] + body + s[end:]; changed = True; total += n[0]
            print(f'f_{fn}: {n[0]} call sites in {f}')
        if '--collision-hooks' in sys.argv:
            patched = collision_hooks(s)
            changed |= patched != s
            s = patched
        if changed: open(f, 'w').write(s)
    if native:
        path, content = native
        path.write_text(content)
        print(f'native object collector: 1716F0 callback in {path}')
    print(f'scene phase timers: {total} call sites')
if __name__ == '__main__': main()
