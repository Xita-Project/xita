#pragma once
/* Startup-only experiment: immutable selection, no runtime override, counters,
 * additional scopes, locks, ownership transfer or independently retained data.
 * OFF retains the original emitted entries. */
#ifdef XV_NATIVE_COLLISION_TRAVERSAL
extern const unsigned xv_collision_traversal_mode;
int xv_collision_traversal_enabled(void);
#endif
