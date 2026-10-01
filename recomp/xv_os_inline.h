/* xv_os_inline.h - force-included into generated shards compiled with -Os: -Os stops inlining these small
 * x87/guest-memory helpers (one out-of-line copy per unit, ~11K extra calls in code_008). Redeclaring them
 * always_inline keeps the -Os size reduction for everything else. No definition or arithmetic changes. */
#pragma once
#include "xv_x86rt.h"
static inline __attribute__((always_inline)) void x87_push(xctx *, double);
static inline __attribute__((always_inline)) void x87_pop(xctx *);
static inline __attribute__((always_inline)) double x87_load_f32(xctx *, uint32_t);
static inline __attribute__((always_inline)) double x87_load_f64(xctx *, uint32_t);
static inline __attribute__((always_inline)) double x87_load_i32(xctx *, uint32_t);
static inline __attribute__((always_inline)) void x87_store_f32(xctx *, uint32_t, double);
static inline __attribute__((always_inline)) void x87_store_f64(xctx *, uint32_t, double);
static inline __attribute__((always_inline)) void x87_store_i32(xctx *, uint32_t, double);
