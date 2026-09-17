#include "xk_collision_traversal.h"
#ifdef XV_NATIVE_COLLISION_TRAVERSAL
#ifndef XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT
#define XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT 0
#endif
#if XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT != 0 && XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT != 1
#error XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT must be 0 or 1
#endif
const unsigned xv_collision_traversal_mode = XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT;
int xv_collision_traversal_enabled(void) { return !!xv_collision_traversal_mode; }
#endif
