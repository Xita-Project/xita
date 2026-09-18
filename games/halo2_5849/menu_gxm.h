/* GXM backend for the general menu draws (same request contract as the software
 * renderer). Shaders are the offline-compiled per-program pairs selected by the
 * hashes the software path logs ("[h2/menu-shader]"):
 *   app0:h2menu_vs_<vp>.gxp  /  app0:h2menu_ps_<ps>_<vp>.frag.gxp
 * Returns 1 drawn, 0 rejected, -1 not taken (missing shader: the caller falls back
 * to the software rasterizer after this backend flushed its pending scene). */
#pragma once
#include "menu_draw.h"

int h2_menu_gxm_render(void *opaque, const h2_menu_request *request);
/* Finish and download any open scene into the guest buffers (before the game
 * clears a surface, flips, or anything else reads the back buffers). */
void h2_menu_gxm_flush(void);
/* The game cleared its zeta surface: the GXM depth surface starts cleared next scene. */
void h2_menu_gxm_zeta_cleared(uint32_t clear_value);
