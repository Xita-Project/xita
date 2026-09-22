#!/usr/bin/env python3
"""Install the model-draw guard at f_000A26B0's entry (idempotent): when the model tag resolved from eax (tag index)
has an insane node count at +0xB8, log the index, tag entry, callers and skip the draw (emulate `ret 2Ch`) instead
of looping forever in the node copy (the overlap hang: ebp 01914660 / node count 41E87F7B on every Pi soak).
Usage: patch_model_draw_guard.py <stage>/recomp"""
import re, sys, glob
GUARD = '''    {   /* model-draw guard (tools/patch_model_draw_guard.py): see the header there */
        static unsigned shown; uint32_t idx_ = c->r[0] & 0xFFFFu, tib_ = X_M32(0x39CE24u), ent_ = tib_ + idx_ * 32u, tag_ = X_M32(ent_ + 0x14u);
        uint32_t nodes_ = tag_ ? X_M32(tag_ + 0xB8u) : 0;
        if (nodes_ > 0x400u) {
            if (shown < 8) { shown++; extern void xk_os_log(const char *, ...); uint32_t sp_ = c->r[4];
                xk_os_log("[model-guard] tag index %04X (eax %08X ecx %08X edx %08X) entry %08X class %08X/%08X/%08X id %08X data %08X: nodes %08X -> skipped; ret %08X args %08X %08X %08X %08X\\n",
                          idx_, c->r[0], c->r[1], c->r[2], ent_, X_M32(ent_), X_M32(ent_ + 4u), X_M32(ent_ + 8u), X_M32(ent_ + 0xCu), tag_, nodes_, X_M32(sp_), X_M32(sp_ + 4u), X_M32(sp_ + 8u), X_M32(sp_ + 12u), X_M32(sp_ + 16u)); }
            c->r[4] += 0x30u; return;   /* ret 2Ch */
        }
    }
'''
def main():
    root = sys.argv[1]
    for f in glob.glob(root + '/code_*.c'):
        s = open(f).read(); m = re.search(r'^void f_000A26B0\(xctx \*restrict c\)\n\{\n', s, re.M)
        if not m: continue
        if 'model-draw guard' in s[m.end():m.end() + 4000]: print('model-draw guard already present'); return
        anchor = '    uint32_t fk_a = 0, fk_b = 0, fk_r = 0; (void)fk_a; (void)fk_b; (void)fk_r;\n'
        i = s.index(anchor, m.end()) + len(anchor)
        open(f, 'w').write(s[:i] + GUARD + s[i:]); print('model-draw guard installed in', f); return
    print('f_000A26B0 not found'); sys.exit(1)
if __name__ == '__main__': main()
