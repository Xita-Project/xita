/* Halo CE point-location prototype. Not hooked into gameplay yet.
 * Keep guest stack writes and every backedge scheduling point. Local doubles
 * replace transient x87 slots; both dead slots are restored before comparison.
 * Compile without fast-math or floating-point contraction. */
#pragma once
/* Preserve operand order for exceptional operands: compilers may exchange
 * commutative local-register operands, changing NaN payload selection. */
static __attribute__((noinline)) void xv_point_plane_exceptional(xctx *c)
{
    x87_push(c, x87_load_f32(c, c->r[0]+8u));
    X_ST(0) = X_ST(0) * x87_load_f32(c, c->r[2]+8u);
    x87_push(c, x87_load_f32(c, c->r[0]+4u));
    X_ST(0) = X_ST(0) * x87_load_f32(c, c->r[2]+4u);
    X_ST(1) = X_ST(1) + X_ST(0); x87_pop(c);
    x87_push(c, x87_load_f32(c, c->r[0]));
    X_ST(0) = X_ST(0) * x87_load_f32(c, c->r[2]);
    X_ST(1) = X_ST(1) + X_ST(0); x87_pop(c);
    X_ST(0) = X_ST(0) - x87_load_f32(c, c->r[0]+12u);
}
static void xv_point_location_body(xctx *c)
{
    X_PUSH32(c->r[6]);
    c->r[6] = X_M32(c->r[1] + 4u);
    X_PUSH32(c->r[7]);
    c->r[7] = X_M32(c->r[1] + 16u);
    for (;;) {
        c->r[1] = c->r[6] + c->r[0] * 12u;
        c->r[0] = x_shl32(c, X_M32(c->r[1]), 4u);
        double z = x87_load_f32(c, c->r[0] + c->r[7] + 8u);
        c->r[0] += c->r[7];
        z *= x87_load_f32(c, c->r[2] + 8u);
        double y = x87_load_f32(c, c->r[0] + 4u);
        y *= x87_load_f32(c, c->r[2] + 4u);
        double d = z + y;
        double x = x87_load_f32(c, c->r[0]);
        x *= x87_load_f32(c, c->r[2]);
        d = d + x;
        d = d - x87_load_f32(c, c->r[0] + 12u);
        if (!isfinite(d) || !isfinite(x)) xv_point_plane_exceptional(c);
        else {
            x87_push(c, d);
            c->st[(c->fsp - 1u) & 7u] = x;
        }
        x87_compare(c, X_ST(0), x87_load_f32(c, 0x1F0A68u), 0);
        x87_pop(c);
        c->r[0] = X_M32(c->r[1] + ((c->fsw & 0x100u) ? 4u : 8u));
        X_FLAGS(XK_LOGIC, 0, 0, c->r[0], 32);
        if (c->r[0] & 0x80000000u) break;
        X_PREEMPT();
    }
    X_FLAGS(XK_SUB, c->r[0], 0xFFFFFFFFu, c->r[0] - 0xFFFFFFFFu, 32);
    c->r[7] = X_POP32();
    c->r[6] = X_POP32();
    if (c->r[0] != 0xFFFFFFFFu) c->r[0] &= 0x7FFFFFFFu;
    c->r[4] += 4;
}
