/* Two passive owner elapsed scopes. Independent of XV_PHASE and worker policy. */
#pragma once
#include <stdint.h>
enum { XV_OWNER_TICK, XV_OWNER_SCENE, XV_OWNER_PHASES };
typedef uint64_t xv_owner_phase_scope;
#ifdef XV_OWNER_PHASE
extern int xv_owner_phase_enabled; /* configured once before threads; no live setter */
void xv_owner_phase_configure(void);
void xv_owner_phase_present(void *context);
void xv_owner_phase_begin(xv_owner_phase_scope *, void *context, unsigned phase);
void xv_owner_phase_end(xv_owner_phase_scope *);
void xv_owner_phase_report(unsigned frames);
#define XV_OWNER_PHASE_SCOPE(context, id) \
    xv_owner_phase_scope xv_owner_phase_scope_ __attribute__((cleanup(xv_owner_phase_end))) = 0; \
    if (xv_owner_phase_enabled) xv_owner_phase_begin(&xv_owner_phase_scope_, (context), (id))
#else
#define XV_OWNER_PHASE_SCOPE(context, id) ((void)0)
#endif
