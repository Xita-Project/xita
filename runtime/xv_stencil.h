/* Xbox stencil state retained with each draw; no guest pointers or GPU calls. */
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t enabled, func, fail, depth_fail, pass, ref, read_mask, write_mask;
} xv_stencil;

/* Compact operation indices follow the GXM operation order. */
static inline uint8_t xv_stencil_op(uint32_t token)
{
    switch (token) {
    case 0: return 1;       /* ZERO */
    case 0x1e01: return 2;  /* REPLACE */
    case 0x1e02: return 3;  /* INCR, saturating */
    case 0x1e03: return 4;  /* DECR, saturating */
    case 0x150a: return 5;  /* INVERT */
    case 0x8507: return 6;  /* INCR_WRAP */
    case 0x8508: return 7;  /* DECR_WRAP */
    default: return 0;      /* KEEP (0x1e00), or unsupported token */
    }
}

static inline xv_stencil xv_stencil_default(void)
{ return (xv_stencil){0, 7, 0, 0, 0, 0, 255, 255}; }

static inline int xv_stencil_method(xv_stencil *s, uint32_t method, uint32_t value)
{
    switch (method & 0x1ffcu) {
    case 0x32c: s->enabled = value != 0; break;
    case 0x360: s->write_mask = (uint8_t)value; break;
    case 0x364: s->func = value >= 0x200 && value <= 0x207 ? value - 0x200 : 7; break;
    case 0x368: s->ref = (uint8_t)value; break;
    case 0x36c: s->read_mask = (uint8_t)value; break;
    case 0x370: s->fail = xv_stencil_op(value); break;
    case 0x374: s->depth_fail = xv_stencil_op(value); break;
    case 0x378: s->pass = xv_stencil_op(value); break;
    default: return 0;
    }
    return 1;
}
