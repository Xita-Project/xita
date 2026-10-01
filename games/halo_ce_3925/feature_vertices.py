"""Optional scoped 868F0 vertex replacement; retain the ordinary body."""
import hashlib
SPAN = (0x868F0, 273, "c09a6bd0479334c5b029bbcdb021322e4200dde15ce3eb53ce65385251fd4dc1")

def entry(image, address):
    pc, size, digest = SPAN
    if address != pc or hashlib.sha256(image.bytes_at(pc, size) or b"").hexdigest() != digest:
        return []
    return [
        "#if defined(XV_NATIVE_FEATURE_VERTICES) && XV_NATIVE_FEATURE_VERTICES",
        "    extern int xv_native_feature_vertices(xctx *, void (*)(xctx *), void (*)(xctx *), void (*)(xctx *));",
        "    extern void f_000855F0(xctx *), f_000862A0(xctx *), f_00086170(xctx *);",
        "    if (xv_native_feature_vertices(c, f_000855F0, f_000862A0, f_00086170)) return;",
        "#endif",
    ]
