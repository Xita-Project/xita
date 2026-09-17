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
/* Bind a zero token only after admission; a nonzero token must still match.
 * -1 unknown/invalid, 0 live owner outside phase, 1 live owner inside phase.
 * No clock reads and no scheduler/worker-policy changes. */
int xv_owner_phase_active(void *context, unsigned phase, uint32_t *generation_token);
#if defined(XV_SCENE_PARTITION) && XV_SCENE_PARTITION
void xv_scene_partition_begin(uint64_t *scope, void *context);
void xv_scene_partition_step(uint64_t *scope, void *context, unsigned bucket);
void xv_scene_partition_end(uint64_t *scope);
#if defined(XV_SCENE_BUCKET0_DETAIL) && XV_SCENE_BUCKET0_DETAIL
void xv_scene_bucket0_step(uint64_t *scope, void *context, unsigned bucket);
#endif
#endif
#define XV_OWNER_PHASE_SCOPE(context, id) \
    xv_owner_phase_scope xv_owner_phase_scope_ __attribute__((cleanup(xv_owner_phase_end))) = 0; \
    if (xv_owner_phase_enabled) xv_owner_phase_begin(&xv_owner_phase_scope_, (context), (id))
#else
#define XV_OWNER_PHASE_SCOPE(context, id) ((void)0)
#endif
