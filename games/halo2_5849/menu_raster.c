#include "menu_raster.h"
#include <math.h>
#include <string.h>

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }

static void sample_tex(const menu_texture *t, float u, float v, float out[4])
{
    if (!t->texels || !t->width || !t->height) { out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f; return; }
    u -= floorf(u); v -= floorf(v);                      /* wrap/repeat (NV2A default) */
    int32_t x = (int32_t)(u * (float)t->width);
    int32_t y = (int32_t)(v * (float)t->height);
    if (x < 0) x = 0;
    if (x >= (int32_t)t->width) x = (int32_t)t->width - 1;
    if (y < 0) y = 0;
    if (y >= (int32_t)t->height) y = (int32_t)t->height - 1;
    uint32_t p = t->texels[(uint32_t)y * t->width + (uint32_t)x];
    out[0] = ((p >> 16) & 0xFF) / 255.0f;
    out[1] = ((p >> 8) & 0xFF) / 255.0f;
    out[2] = (p & 0xFF) / 255.0f;
    out[3] = ((p >> 24) & 0xFF) / 255.0f;
}

void menu_raster_triangle(const menu_raster_state *st, const menu_vertex_out *a,
                          const menu_vertex_out *b, const menu_vertex_out *c)
{
    const menu_target *tg = &st->target;
    if (!tg->pixels || !tg->width || !tg->height) return;

    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (area == 0.0f) return;
    float inv_area = 1.0f / area;

    int32_t minx = (int32_t)floorf(fminf(a->x, fminf(b->x, c->x)));
    int32_t maxx = (int32_t)ceilf(fmaxf(a->x, fmaxf(b->x, c->x)));
    int32_t miny = (int32_t)floorf(fminf(a->y, fminf(b->y, c->y)));
    int32_t maxy = (int32_t)ceilf(fmaxf(a->y, fmaxf(b->y, c->y)));
    int32_t cx0 = imax(0, imax(minx, st->clip_x0));
    int32_t cy0 = imax(0, imax(miny, st->clip_y0));
    int32_t cx1 = imin((int32_t)tg->width - 1, imin(maxx, st->clip_x1));
    int32_t cy1 = imin((int32_t)tg->height - 1, imin(maxy, st->clip_y1));

    float iwa = a->w > 0.0f ? 1.0f / a->w : 1.0f;
    float iwb = b->w > 0.0f ? 1.0f / b->w : 1.0f;
    float iwc = c->w > 0.0f ? 1.0f / c->w : 1.0f;
    const float fog[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    for (int32_t py = cy0; py <= cy1; ++py) {
        float fy = (float)py + 0.5f;
        uint8_t *row = tg->pixels + (uint32_t)py * tg->pitch;
        for (int32_t px = cx0; px <= cx1; ++px) {
            float fx = (float)px + 0.5f;
            float w0 = ((b->x - fx) * (c->y - fy) - (b->y - fy) * (c->x - fx)) * inv_area;
            float w1 = ((c->x - fx) * (a->y - fy) - (c->y - fy) * (a->x - fx)) * inv_area;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

            float iw = w0 * iwa + w1 * iwb + w2 * iwc;
            if (iw <= 0.0f) continue;
            float inv_iw = 1.0f / iw;
            float b0 = w0 * iwa * inv_iw, b1 = w1 * iwb * inv_iw, b2 = w2 * iwc * inv_iw;

            float diffuse[4], specular[4], texf[4][4], frag[4];
            for (unsigned k = 0; k < 4; ++k) {
                diffuse[k] = b0 * a->color[k] + b1 * b->color[k] + b2 * c->color[k];
                specular[k] = b0 * a->specular[k] + b1 * b->specular[k] + b2 * c->specular[k];
            }
            for (unsigned u = 0; u < 4; ++u) {
                float uu = b0 * a->uv[u][0] + b1 * b->uv[u][0] + b2 * c->uv[u][0];
                float vv = b0 * a->uv[u][1] + b1 * b->uv[u][1] + b2 * c->uv[u][1];
                sample_tex(&st->tex[u], uu, vv, texf[u]);
            }
            if (st->combiner)
                menu_combiner_eval(st->combiner, (const float (*)[4])texf, diffuse, specular, fog, frag);
            else
                for (unsigned k = 0; k < 4; ++k) frag[k] = clampf(diffuse[k], 0.0f, 1.0f);

            uint8_t *p = row + (uint32_t)px * 4;
            if (st->blend == MENU_BLEND_ALPHA) {
                float ca = clampf(frag[3], 0.0f, 1.0f), ia = 1.0f - ca;
                p[0] = (uint8_t)(clampf(frag[2], 0, 1) * 255.0f * ca + p[0] * ia);
                p[1] = (uint8_t)(clampf(frag[1], 0, 1) * 255.0f * ca + p[1] * ia);
                p[2] = (uint8_t)(clampf(frag[0], 0, 1) * 255.0f * ca + p[2] * ia);
            } else {
                p[0] = (uint8_t)(clampf(frag[2], 0, 1) * 255.0f);
                p[1] = (uint8_t)(clampf(frag[1], 0, 1) * 255.0f);
                p[2] = (uint8_t)(clampf(frag[0], 0, 1) * 255.0f);
            }
        }
    }
}
