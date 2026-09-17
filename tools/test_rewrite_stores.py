"""Memory lvalues become X_W<width> (stamped stores in checked builds); loads stay X_M<width>."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "recompiler"))
import xita_recomp

cls = next(v for v in vars(xita_recomp).values() if isinstance(v, type) and hasattr(v, "rewrite_stores"))
rw = cls.rewrite_stores

cases = [
    ("    X_M32((c->r[4]+0x1Cu)) = c->r[0];", "    X_W32((c->r[4]+0x1Cu)) = c->r[0];"),
    ("    c->r[0] = X_M32((c->r[4]+0x1Cu));", "    c->r[0] = X_M32((c->r[4]+0x1Cu));"),
    ("    X_M8(c->r[3] + X_R8L(0)) = (uint8_t)v;", "    X_W8(c->r[3] + X_R8L(0)) = (uint8_t)v;"),
    ("    if (X_M32(a) == X_M32(b)) {", "    if (X_M32(a) == X_M32(b)) {"),
    ("    X_M16((c->r[4])) = X_M16((c->r[5]));", "    X_W16((c->r[4])) = X_M16((c->r[5]));"),
    ("    X_MF32(c->r[4]) = X_MF32(c->r[6]) + 1.0f;", "    X_WF32(c->r[4]) = X_MF32(c->r[6]) + 1.0f;"),
    ("    X_M64((c->r[4] + f(x, (y)))) = q;", "    X_W64((c->r[4] + f(x, (y)))) = q;"),
    ("    X_MOVQ(x) = 1; X_M(y) = 2; X_M32x(z) = 3;", "    X_MOVQ(x) = 1; X_M(y) = 2; X_M32x(z) = 3;"),
    ("    X_M32(a) <= 3;", "    X_M32(a) <= 3;"),
]
for src, want in cases:
    got = rw(src)
    assert got == want, (src, got, want)
multi = "\n".join(s for s, _ in cases)
assert rw(multi) == "\n".join(w for _, w in cases)
print("rewrite_stores: %d cases passed" % len(cases))
