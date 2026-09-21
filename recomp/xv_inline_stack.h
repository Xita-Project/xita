/* xv_inline_stack.h - force-included into the two rendering guest units selected by the Makefile
 * (XV_RENDER_INLINE_STACK=1, the units defining 70110 and 7E530, which XV_RENDER_GUEST_SIZE compiles
 * with -Os). Under -Os GCC stops inlining the x87 stack helpers; redeclaring them always_inline removes
 * their call sites in the material routine (probe: ../render-inline-stack-probe, +1.76% unit text).
 * The definitions in xv_x86rt.h are unchanged; no arithmetic changes. */
#pragma once
#include "xv_x86rt.h"
static inline __attribute__((always_inline)) void x87_push(xctx *, double);
static inline __attribute__((always_inline)) void x87_pop(xctx *);
