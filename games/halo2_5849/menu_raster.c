#include "menu_raster.h"
#include <math.h>
#include <string.h>

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }
static uint8_t to8(float v) { return (uint8_t)(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

static void sample_tex(const menu_texture *t, float u, float v, float out[4])
{
    /* An unused/disabled stage reads as all zeros (alpha included): the combiner
     * sums stage products, so a phantom alpha of 1 would saturate e.g. the font
     * cache's coverage (t2*c0 + t0*v0 in alpha) into solid boxes. */
    if (!t->texels || !t->width || !t->height) { out[0] = out[1] = out[2] = out[3] = 0.0f; return; }
    int32_t x, y;
    if (t->texel_coords) {                               /* linear image: texel units */
        x = (int32_t)floorf(u); y = (int32_t)floorf(v);
    } else {
        u -= floorf(u); v -= floorf(v);                  /* wrap/repeat (NV2A default) */
        x = (int32_t)(u * (float)t->width);
        y = (int32_t)(v * (float)t->height);
    }
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

/* GL compare enums shared by the alpha and depth tests: a OP b. */
static int compare(uint32_t func, double a, double b)
{
    switch (func) {
    case 0x200: return 0;
    case 0x201: return a < b;
    case 0x202: return a == b;
    case 0x203: return a <= b;
    case 0x204: return a > b;
    case 0x205: return a != b;
    case 0x206: return a >= b;
    default:    return 1;                                /* 0x207 ALWAYS */
    }
}

static int depth_pass(uint32_t func, uint32_t z, uint32_t stored)
{
    const int64_t eps = 16;                              /* 24-bit units; float z jitter between passes */
    int64_t d = (int64_t)z - (int64_t)stored;
    switch (func) {
    case 0x200: return 0;
    case 0x201: return d < 0;
    case 0x202: return d >= -eps && d <= eps;
    case 0x203: return d <= eps;
    case 0x204: return d > 0;
    case 0x205: return d < -eps || d > eps;
    case 0x206: return d >= -eps;
    default:    return 1;
    }
}

static int factor_supported(uint32_t f)
{
    return f <= 1 || (f >= 0x300 && f <= 0x308) || (f >= 0x8001 && f <= 0x8004);
}

int menu_raster_blend_supported(uint32_t sfactor, uint32_t dfactor, uint32_t equation)
{
    if (!factor_supported(sfactor) || !factor_supported(dfactor)) return 0;
    switch (equation) {
    case 0x8006: case 0x8007: case 0x8008: case 0x800A: case 0x800B: case 0xF005: case 0xF006: return 1;
    default: return 0;
    }
}

static void blend_factor(uint32_t f, const float s[4], const float d[4], const float cc[4], float out[4])
{
    for (unsigned k = 0; k < 4; ++k) {
        float v;
        switch (f) {
        case 0:      v = 0.0f; break;
        case 0x300:  v = s[k]; break;
        case 0x301:  v = 1.0f - s[k]; break;
        case 0x302:  v = s[3]; break;
        case 0x303:  v = 1.0f - s[3]; break;
        case 0x304:  v = d[3]; break;
        case 0x305:  v = 1.0f - d[3]; break;
        case 0x306:  v = d[k]; break;
        case 0x307:  v = 1.0f - d[k]; break;
        case 0x308:  v = k == 3 ? 1.0f : fminf(s[3], 1.0f - d[3]); break; /* SRC_ALPHA_SATURATE */
        case 0x8001: v = cc[k]; break;
        case 0x8002: v = 1.0f - cc[k]; break;
        case 0x8003: v = cc[3]; break;
        case 0x8004: v = 1.0f - cc[3]; break;
        default:     v = 1.0f; break;                     /* 1 = ONE */
        }
        out[k] = v;
    }
}

static void blend_pixel(const menu_raster_state *st, const float src[4], uint8_t *p)
{
    float dst[4] = {p[2] / 255.0f, p[1] / 255.0f, p[0] / 255.0f, p[3] / 255.0f};
    float out[4];
    if (st->blend == MENU_BLEND_OPAQUE) {
        memcpy(out, src, sizeof out);
    } else {
        uint32_t sf = 0x302, df = 0x303, eq = 0x8006;    /* MENU_BLEND_ALPHA */
        float cc[4] = {0, 0, 0, 0}, fs[4], fd[4];
        if (st->blend == MENU_BLEND_FUNC) {
            sf = st->sfactor; df = st->dfactor; eq = st->equation;
            cc[0] = ((st->blend_color >> 16) & 0xFF) / 255.0f;
            cc[1] = ((st->blend_color >> 8) & 0xFF) / 255.0f;
            cc[2] = (st->blend_color & 0xFF) / 255.0f;
            cc[3] = (st->blend_color >> 24) / 255.0f;
        }
        blend_factor(sf, src, dst, cc, fs);
        blend_factor(df, src, dst, cc, fd);
        for (unsigned k = 0; k < 4; ++k) {
            float s = clampf(src[k], 0.0f, 1.0f) * fs[k], d = dst[k] * fd[k];
            switch (eq) {
            case 0x8007: out[k] = fminf(clampf(src[k], 0.0f, 1.0f), dst[k]); break; /* MIN */
            case 0x8008: out[k] = fmaxf(clampf(src[k], 0.0f, 1.0f), dst[k]); break; /* MAX */
            case 0x800A: out[k] = s - d; break;                                      /* SUBTRACT */
            case 0x800B: case 0xF005: out[k] = d - s; break;                         /* REVERSE_SUBTRACT */
            default:     out[k] = s + d; break;                                      /* ADD (0x8006/0xF006) */
            }
        }
    }
    p[0] = to8(out[2]); p[1] = to8(out[1]); p[2] = to8(out[0]); p[3] = to8(out[3]);
}

void (*menu_raster_parallel)(menu_raster_band_fn fn, const void *job, int32_t y0, int32_t y1);
void (*menu_raster_tick)(void);

typedef struct {
    const menu_raster_state *st;
    const menu_vertex_out *a, *b, *c;
    const menu_depth *dp;
    float inv_area, iwa, iwb, iwc;
    int32_t cx0, cx1;
    unsigned tex_mask;                 /* units the combiner reads; others sample as zero */
} tri_job;

uint64_t menu_raster_zpass;
static void raster_band(const void *vjob, int32_t cy0, int32_t cy1, int on_caller)
{
    const tri_job *J = vjob;
    const menu_raster_state *st = J->st;
    const menu_target *tg = &st->target;
    const menu_vertex_out *a = J->a, *b = J->b, *c = J->c;
    const menu_depth *dp = J->dp;
    const float inv_area = J->inv_area, iwa = J->iwa, iwb = J->iwb, iwc = J->iwc;
    const int32_t cx0 = J->cx0, cx1 = J->cx1;
    const float fog[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    for (int32_t py = cy0; py <= cy1; ++py) {
        if (on_caller && menu_raster_tick && !((py - cy0) & 7)) menu_raster_tick();
        float fy = (float)py + 0.5f;
        uint8_t *row = tg->pixels + (uint32_t)py * tg->pitch;
        uint32_t *zrow = dp ? (uint32_t *)(void *)((uint8_t *)dp->pixels + (uint32_t)py * dp->pitch) : NULL;
        for (int32_t px = cx0; px <= cx1; ++px) {
            float fx = (float)px + 0.5f;
            float w0 = ((b->x - fx) * (c->y - fy) - (b->y - fy) * (c->x - fx)) * inv_area;
            float w1 = ((c->x - fx) * (a->y - fy) - (c->y - fy) * (a->x - fx)) * inv_area;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

            /* Window z interpolates linearly in screen space (NV2A fixed 24-bit). */
            uint32_t z24 = 0;
            if (zrow) {
                double z = (double)w0 * a->z + (double)w1 * b->z + (double)w2 * c->z;
                z = z < 0.0 ? 0.0 : z > 1.0 ? 1.0 : z;
                z24 = (uint32_t)(z * 16777215.0 + 0.5);
                if (!depth_pass(dp->func, z24, zrow[px] >> 8)) continue;
            }

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
                if (!(J->tex_mask & (1u << u))) { texf[u][0] = texf[u][1] = texf[u][2] = texf[u][3] = 0.0f; continue; }
                float uu = b0 * a->uv[u][0] + b1 * b->uv[u][0] + b2 * c->uv[u][0];
                float vv = b0 * a->uv[u][1] + b1 * b->uv[u][1] + b2 * c->uv[u][1];
                sample_tex(&st->tex[u], uu, vv, texf[u]);
            }
            if (st->combiner)
                menu_combiner_eval(st->combiner, (const float (*)[4])texf, diffuse, specular, fog, frag);
            else
                for (unsigned k = 0; k < 4; ++k) frag[k] = clampf(diffuse[k], 0.0f, 1.0f);

            if (st->alpha_test && !compare(st->alpha_func, (double)clampf(frag[3], 0.0f, 1.0f), (double)st->alpha_ref))
                continue;
            if (st->zpass_count) __atomic_fetch_add(&menu_raster_zpass, 1u, __ATOMIC_RELAXED);
            if (zrow && dp->write) zrow[px] = (z24 << 8) | (zrow[px] & 0xFF);

            blend_pixel(st, frag, row + (uint32_t)px * 4);
        }
    }
}

void menu_raster_triangle(const menu_raster_state *st, const menu_vertex_out *a,
                          const menu_vertex_out *b, const menu_vertex_out *c)
{
    const menu_target *tg = &st->target;
    if (!tg->pixels || !tg->width || !tg->height) return;

    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (area == 0.0f) return;
    if (st->cull_enable) {
        /* Window Y grows down: positive signed area is clockwise. */
        int front = (area > 0.0f) == (st->front_face == 0x900);
        if (st->cull_face == 0x408 ||
            (st->cull_face == 0x404 && front) ||
            (st->cull_face == 0x405 && !front)) return;
    }

    int32_t minx = (int32_t)floorf(fminf(a->x, fminf(b->x, c->x)));
    int32_t maxx = (int32_t)ceilf(fmaxf(a->x, fmaxf(b->x, c->x)));
    int32_t miny = (int32_t)floorf(fminf(a->y, fminf(b->y, c->y)));
    int32_t maxy = (int32_t)ceilf(fmaxf(a->y, fmaxf(b->y, c->y)));
    int32_t cy0 = imax(0, imax(miny, st->clip_y0));
    int32_t cy1 = imin((int32_t)tg->height - 1, imin(maxy, st->clip_y1));

    tri_job J;
    J.st = st; J.a = a; J.b = b; J.c = c;
    J.dp = st->depth.pixels ? &st->depth : NULL;
    if (J.dp && (J.dp->width < tg->width || J.dp->height < tg->height)) J.dp = NULL;
    J.inv_area = 1.0f / area;
    J.iwa = a->w > 0.0f ? 1.0f / a->w : 1.0f;
    J.iwb = b->w > 0.0f ? 1.0f / b->w : 1.0f;
    J.iwc = c->w > 0.0f ? 1.0f / c->w : 1.0f;
    J.cx0 = imax(0, imax(minx, st->clip_x0));
    J.cx1 = imin((int32_t)tg->width - 1, imin(maxx, st->clip_x1));
    /* Without a prepared combiner (diffuse passthrough or raw test programs) sample every bound unit. */
    J.tex_mask = (st->combiner && st->combiner->prepared) ? st->combiner->tex_used : 0xFu;
    if (cy0 > cy1 || J.cx0 > J.cx1) return;

    /* Rows are independent (per-pixel colour/depth writes), so large triangles
     * are split into row bands across the worker pool when one is installed. */
    if (menu_raster_parallel && cy1 - cy0 >= 24 && (int64_t)(cy1 - cy0) * (J.cx1 - J.cx0) >= 2048)
        menu_raster_parallel(raster_band, &J, cy0, cy1);
    else
        raster_band(&J, cy0, cy1, 1);
}
