#pragma once
#include "../xv_x86rt.h"

/* Pinned 534D5 caller only. Zero preserves all guest state for B7F10 fallback.
 * One supplies the live signed count/output and completes its 24-byte return.
 * This is NOT a general B7F10 ABI or an interior-entry replacement. */
int xv_portal_polygon(xctx *c);
void xv_portal_polygon_report(unsigned frames);
