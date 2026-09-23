#!/usr/bin/env python3
"""Install the XV_SCENE_PHASES timers around the direct guest calls of the scene entry f_000BCB30 and of its main callee
f_0005DBC0 in a stage's hand-maintained shards (idempotent: earlier timers are stripped and re-installed).
Usage: patch_scene_phase_timers.py <stage>/recomp [--parents A,B]     Report: [scene-phases] in recomp/kernel/xk_scene_thread.c."""
import re, sys, glob
PARENTS = ['000900E0', '0014A162', '000B9678', '000B8840',   # the sim tick's two big callees (38 + 14 ms/frame, perf125) and the Pi sampler's hot owner chain (B9678 -> B8980 66%)
           '00109760', '0005B4A0', '00062240', '000A26B0',   # sim tick (2-3/frame at 12 fps), per-model chain, the 182/frame callee (perf124 split)
           '000FA920',   # the tick root (54 ms/frame in the a10 cinematic, perf122): its direct callees are the tick's phases
           '000BD420',   # the owner's frame loop (tick side): its direct callees are the tick phases ([tick-phases])
           '000BCB30', '0005DBC0', '0005D990', '0005C5E0', '0005D410', '0005BCB0', '00028320',
           '000606B0', '00054010', '00060560', '0005B760', '00054740', '0005B710', '000539C0', '00092890', '00093C00']   # + the Vita's top scene callees (§47), one level down   # scene entry, its main callee, and the chain below (each level was one callee on the host)
BEGIN = '    { extern void xv_scene_phase_begin(uint32_t); xv_scene_phase_begin(0x%su); }\n'
def main():
    root = sys.argv[1]; total = 0
    global PARENTS
    if '--parents' in sys.argv:   # e.g. --parents 000BD420,000FA920: only these loops (a few calls/frame, no measurable cost)
        PARENTS = sys.argv[sys.argv.index('--parents') + 1].split(',')
    for f in glob.glob(root + '/code_*.c'):
        s = open(f).read(); changed = False
        for fn in PARENTS:
            m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, s, re.M)
            if not m: continue
            end = s.find('\nvoid f_', m.end()); body = s[m.end():end]
            body = re.sub(r'^    \{ extern void xv_scene_phase_(begin|end)\([^\n]*\n', '', body, flags=re.M)   # strip old (both the old void begin and the new addr begin)
            n = [0]
            def wrap(mm):
                n[0] += 1
                return (BEGIN % mm.group(2)) + mm.group(1) + '    { extern void xv_scene_phase_end(uint32_t); xv_scene_phase_end(0x%su); }\n' % mm.group(2)
            body = re.sub(r'(    X_PUSH32\(0x[0-9A-Fa-f]+u\);\n    f_([0-9A-F]{8})\(c\);\n)', wrap, body)
            s = s[:m.end()] + body + s[end:]; changed = True; total += n[0]
            print(f'f_{fn}: {n[0]} call sites in {f}')
        if changed: open(f, 'w').write(s)
    print(f'scene phase timers: {total} call sites')
if __name__ == '__main__': main()
