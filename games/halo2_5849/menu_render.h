/* Software menu backend: h2_menu_render implementation.
 *
 * Wired as menu_quad.render. For each menu draw it maps the game's back buffer,
 * resolves the active vertex arrays (or uses the immediate vertices), runs the
 * captured NV2A vertex program per vertex (nv2a_vsh), assembles triangles for
 * the primitive, and rasterizes them (menu_raster) with a simplified combiner.
 * All guest reads go through the clear's validated DMA/mapper; a resource that
 * cannot be resolved makes the draw reject (return 0) without mutation. */
#pragma once
#include "menu_draw.h"

int h2_menu_software_render(void *opaque, const h2_menu_request *request);
