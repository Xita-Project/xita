/* Selected guest scopes. Normal generation contains no scope sites. */
#pragma once
#include <stdint.h>

enum { XV_PHASE_MAX_TARGETS = 48, XV_PHASE_MAX_OWNERS = 32, XV_PHASE_MAX_DEPTH = 32 };
typedef struct { uint32_t address; const char *name; } xv_phase_target;
typedef struct xv_phase_scope {
    struct xv_phase_scope *previous;
    void *owner;
    unsigned generation, id;
} xv_phase_scope;

extern int xv_phase_enabled;
void xv_phase_init(void);
void xv_phase_begin(xv_phase_scope *scope, void *context, unsigned id);
void xv_phase_end(xv_phase_scope *scope);
void xv_phase_suspend(void *context);
void xv_phase_resume(void *context);
void xv_phase_forget(void *context);
void xv_phase_frame(unsigned end_frame);

static inline void xv_phase_cleanup(xv_phase_scope *scope)
{
    if (scope->owner) xv_phase_end(scope);
}
/* GCC/Clang cleanup covers all ordinary returns, including native early exits
 * and tail calls. Thread termination explicitly forgets its live scope chain. */
#define XV_PHASE_SCOPE(context, index) \
    xv_phase_scope xv_phase_scope_ __attribute__((cleanup(xv_phase_cleanup))) = {0}; \
    if (xv_phase_enabled) xv_phase_begin(&xv_phase_scope_, (context), (index))
