/* Software triangle rasterizer for the main-menu backend.
 *
 * Vertices arrive already transformed to screen space by the NV2A vertex-program
 * interpreter (nv2a_vsh): x,y in pixels, z in [0,1], w the clip w for
 * perspective-correct interpolation. This rasterizes triangles into the game's
 * A8R8G8B8 back buffer (byte order B,G,R,A) with a simplified combiner
 * (diffuse * optional texture0) and standard UI alpha blend. GXM is not used, so
 * the menu path needs no new GXM shaders; the game's own flip presents the buffer.
 * A NULL texture yields flat diffuse shading. */
#pragma once
#include <stdint.h>

typedef struct menu_target {
    uint8_t *pixels;              /* A8R8G8B8, bytes B,G,R,A */
    uint32_t width, height, pitch; /* pitch in bytes */
} menu_target;

typedef struct menu_texture {
    const uint32_t *texels;      /* decoded 0xAARRGGBB, row-major; NULL => untextured */
    uint32_t width, height;
} menu_texture;

typedef struct menu_vertex_out {
    float x, y, z, w;            /* screen-space position (x,y pixels), clip w */
    float color[4];              /* oD0 RGBA in [0,1] */
    float uv[2];                 /* oT0 texcoord */
} menu_vertex_out;

enum { MENU_BLEND_OPAQUE = 0, MENU_BLEND_ALPHA = 1 };

typedef struct menu_raster_state {
    menu_target target;
    menu_texture tex0;
    int blend;                   /* MENU_BLEND_* */
    int32_t clip_x0, clip_y0, clip_x1, clip_y1; /* inclusive scissor in pixels */
} menu_raster_state;

/* Rasterize one triangle (screen-space vertices, CCW or CW both filled). */
void menu_raster_triangle(const menu_raster_state *st, const menu_vertex_out *a,
                          const menu_vertex_out *b, const menu_vertex_out *c);
