/* Software triangle rasterizer for the main-menu backend.
 *
 * Vertices arrive already transformed to screen space by the NV2A vertex-program
 * interpreter: x,y in pixels, z in [0,1], w the clip w for perspective-correct
 * interpolation. Each fragment is depth-tested against the game's own Z24S8
 * zeta surface (when bound), shaded by the NV2A register combiner
 * (menu_combiner) over the four sampled textures and the interpolated
 * diffuse/specular, alpha-tested, then blended with the game's captured blend
 * state into its A8R8G8B8 back buffer (byte order B,G,R,A). GXM is not used;
 * the game's own flip presents the buffer. */
#pragma once
#include <stdint.h>
#include "menu_combiner.h"

typedef struct menu_target {
    uint8_t *pixels;               /* A8R8G8B8, bytes B,G,R,A */
    uint32_t width, height, pitch; /* pitch in bytes */
} menu_target;

typedef struct menu_texture {
    const uint32_t *texels;        /* decoded 0xAARRGGBB, row-major; NULL => unused unit (samples 0,0,0,0) */
    uint32_t width, height;
    int texel_coords;              /* linear image: uv are texel units (clamped), not [0,1] wrap */
} menu_texture;

typedef struct menu_depth {
    uint32_t *pixels;              /* Z24S8 zeta surface (depth << 8 | stencil); NULL => no test */
    uint32_t width, height, pitch; /* pitch in bytes */
    uint32_t func;                 /* NV097_SET_DEPTH_FUNC GL enum 0x200..0x207 */
    int write;                     /* NV097_SET_DEPTH_MASK */
} menu_depth;

typedef struct menu_vertex_out {
    float x, y, z, w;              /* screen-space position (x,y pixels), clip w */
    float color[4];                /* oD0 diffuse RGBA */
    float specular[4];             /* oD1 specular RGBA */
    float uv[4][2];                /* oT0..oT3 texcoords */
} menu_vertex_out;

/* OPAQUE writes the fragment; ALPHA is the fixed src-alpha/one-minus-src-alpha
 * pair; FUNC evaluates the captured NV2A factors/equation below. */
enum { MENU_BLEND_OPAQUE = 0, MENU_BLEND_ALPHA = 1, MENU_BLEND_FUNC = 2 };

typedef struct menu_raster_state {
    menu_target target;
    menu_texture tex[4];
    const menu_combiner *combiner; /* fragment shader; NULL => diffuse passthrough */
    int blend;                     /* MENU_BLEND_* */
    uint32_t sfactor, dfactor;     /* NV097_SET_BLEND_FUNC_* GL enums (MENU_BLEND_FUNC) */
    uint32_t equation;             /* NV097_SET_BLEND_EQUATION GL enum (MENU_BLEND_FUNC) */
    uint32_t blend_color;          /* NV097_SET_BLEND_COLOR, ARGB */
    int alpha_test;                /* NV097_SET_ALPHA_TEST_ENABLE */
    uint32_t alpha_func;           /* NV097_SET_ALPHA_FUNC GL enum */
    float alpha_ref;               /* NV097_SET_ALPHA_REF / 255 */
    menu_depth depth;
    int32_t clip_x0, clip_y0, clip_x1, clip_y1; /* inclusive scissor in pixels */
} menu_raster_state;

/* Blend factors/equations the rasterizer evaluates; others must be rejected by
 * the caller (it falls back to MENU_BLEND_ALPHA and logs) rather than guessed. */
int menu_raster_blend_supported(uint32_t sfactor, uint32_t dfactor, uint32_t equation);

void menu_raster_triangle(const menu_raster_state *st, const menu_vertex_out *a,
                          const menu_vertex_out *b, const menu_vertex_out *c);
