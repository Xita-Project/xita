#pragma once
#include "../xv_x86rt.h"

enum {
    XV_FLARE_NEXT, XV_FLARE_QUERY, XV_FLARE_BRIGHTNESS,
    XV_FLARE_IDENTITY, XV_FLARE_RESET, XV_FLARE_PRESENT,
    XV_FLARE_COLLECTION, XV_FLARE_BARRIERS
};

/* Only the version-checked Halo 3925 result loop can create pending work. */
int xv_flare_defer(xctx *c);
void xv_flare_barrier(unsigned reason);
void xv_flare_defer_override(int enabled);
void xv_flare_report(unsigned frames);
