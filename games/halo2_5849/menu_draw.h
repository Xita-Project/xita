/* General main-menu geometry capture: NV2A BEGIN_END plus immediate vertex,
 * indexed and array emission are assembled into one draw descriptor and handed
 * to a host render backend. Unlike the pinned intro modules (quad/sprite/...),
 * this path does not match a single captured contract; it decodes whatever
 * primitive/vertices the pushbuffer supplies so the menu's many varying draws
 * can be rendered. It performs NO guest or framebuffer mutation itself: the
 * render backend owns all resource mapping and drawing, and a rejected or
 * unbacked draw leaves command state and guest bytes unchanged. */
#pragma once
#include "command_state.h"
#include "kelvin_clear.h"

/* Immediate vertices are small (per-glyph/box quads); indexed geometry carries
 * its vertices in guest arrays the backend fetches, so only indices are held. */
#define H2_MENU_MAX_VERTICES 512u
#define H2_MENU_MAX_INDICES 65536u
#define H2_MENU_ATTRIBUTES 16u

/* One assembled immediate vertex: every NV2A attribute expanded to float4 with
 * the standard defaults (0,0,0,1). Attribute 0 is position, 3/9 the observed
 * texcoord/diffuse; the backend selects the ones its shader consumes. */
typedef struct h2_menu_vertex {
    float attribute[H2_MENU_ATTRIBUTES][4];
} h2_menu_vertex;

/* Resolved draw handed to the backend. Immediate draws populate vertices; an
 * indexed draw populates indices (into guest vertex arrays described by
 * state->setup 0x1720/0x1760) and array draws the start/count range. state and
 * clear expose the full captured pipeline (combiner, textures, render target). */
typedef struct h2_menu_request {
    uint16_t primitive;                 /* NV2A BEGIN_END primitive (5,6,7,8,...) */
    uint32_t vertex_count;              /* immediate vertices assembled */
    const h2_menu_vertex *vertices;
    uint32_t index_count;              /* ARRAY_ELEMENT16/32 indices captured */
    const uint16_t *indices;
    uint32_t array_start, array_count; /* DRAW_ARRAYS range (0 if unused) */
    uint32_t inline_words;             /* INLINE_ARRAY dword count (0 if unused) */
    const h2_command_state *state;
    const h2_kelvin_clear *clear;
} h2_menu_request;

/* Render and commit the draw into the bound render target. Return 1 on a fully
 * validated, completed draw, 0 to reject (leaving guest/state unchanged). The
 * backend must not mutate the request or any guest source it does not own. */
typedef int (*h2_menu_render)(void *opaque, const h2_menu_request *request);

typedef struct h2_menu_draw {
    uint8_t active, subchannel, overflow;
    uint8_t has_immediate, has_indexed, has_array, has_inline;
    uint16_t primitive;
    uint64_t completed, rejected;
    float current[H2_MENU_ATTRIBUTES][4]; /* live "current vertex" attributes */
    h2_menu_vertex vertices[H2_MENU_MAX_VERTICES];
    uint32_t vertex_count;
    uint16_t indices[H2_MENU_MAX_INDICES];
    uint32_t index_count;
    uint32_t array_start, array_count, inline_words;
    h2_menu_render render;
    void *opaque;
} h2_menu_draw;

/* Call for Kelvin methods while active, or for BEGIN_END. Returns 1 (consumed),
 * 0 (rejected: mine but invalid/unbacked), or -1 (not a geometry method for this
 * module; the dispatcher should try state capture). END renders only after a
 * successful backend commit; any rejection preserves state and guest bytes. */
int h2_menu_method(h2_menu_draw *draw, h2_command_state *state, h2_kelvin_clear *clear,
                   uint8_t subchannel, uint16_t method, uint32_t value);
