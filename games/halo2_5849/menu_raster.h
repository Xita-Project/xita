/* Software triangle rasterizer for the main-menu backend.
 *
 * Vertices arrive already transformed to screen space by the NV2A vertex-program
 * interpreter: x,y in pixels, z in [0,1], w the clip w for perspective-correct
 * interpolation. Each fragment is shaded by the NV2A register combiner
 * (menu_combiner) over the four sampled textures and the interpolated
 * diffuse/specular, then alpha-blended into the game's A8R8G8B8 back buffer
 * (byte order B,G,R,A). GXM is not used; the game's own flip presents the buffer. */
#pragma once
#include <stdint.h>
#include "menu_combiner.h"

typedef struct menu_target {
    uint8_t *pixels;               /* A8R8G8B8, bytes B,G,R,A */
    uint32_t width, height, pitch; /* pitch in bytes */
} menu_target;

typedef struct menu_texture {
    const uint32_t *texels;        /* decoded 0xAARRGGBB, row-major; NULL => unused unit */
    uint32_t width, height;
} menu_texture;

typedef struct menu_vertex_out {
    float x, y, z, w;              /* screen-space position (x,y pixels), clip w */
    float color[4];                /* oD0 diffuse RGBA */
    float specular[4];             /* oD1 specular RGBA */
    float uv[4][2];                /* oT0..oT3 texcoords */
} menu_vertex_out;

enum { MENU_BLEND_OPAQUE = 0, MENU_BLEND_ALPHA = 1 };

typedef struct menu_raster_state {
    menu_target target;
    menu_texture tex[4];
    const menu_combiner *combiner; /* fragment shader; NULL => diffuse passthrough */
    int blend;                     /* MENU_BLEND_* */
    int32_t clip_x0, clip_y0, clip_x1, clip_y1; /* inclusive scissor in pixels */
} menu_raster_state;

void menu_raster_triangle(const menu_raster_state *st, const menu_vertex_out *a,
                          const menu_vertex_out *b, const menu_vertex_out *c);
