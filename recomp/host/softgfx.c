/*
 * softgfx.c - host-only software renderer for the recompiled game's immediate-mode UI path.
 * Runs the actual Halo UI vertex-shader program (decl 0x1E13EC family: pos = dph rows c[-68..-65],
 * uv = ((c[-62].x*v0 + v4*c[-62].y) * c[-59].xy + c[-61].zw) * c[-64].xy, color = v9) using the captured
 * SetVertexShaderConstant values, rasterizes the quads as triangles, and dumps softgfx_NNNN.ppm.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../kernel/xk.h"
#include "../kernel/xd3d.h"

#define W 640
#define H 480
static uint32_t g_fb[W * H];          /* 0xAABBGGRR */
static int g_dumped;

/* ---- texture decode (guest X_D3DPixelContainer -> RGBA8 cache) ---------------------------- */
typedef struct { uint32_t hdr, fmtword; int w, h; uint32_t *px; int alpha_only; } texcache_t;
static texcache_t g_tex[512]; static int g_ntex;

static uint32_t morton1(uint32_t x) { x &= 0x55555555; x = (x | (x >> 1)) & 0x33333333; x = (x | (x >> 2)) & 0x0F0F0F0F; x = (x | (x >> 4)) & 0x00FF00FF; x = (x | (x >> 8)) & 0x0000FFFF; return x; }
static void unswizzle_xy(unsigned w, unsigned h, unsigned idx, unsigned *x, unsigned *y)
{
    unsigned bx = morton1(idx), by = morton1(idx >> 1);
    unsigned lw = 0, lh = 0, t;
    for (t = w; t > 1; t >>= 1) lw++;
    for (t = h; t > 1; t >>= 1) lh++;
    unsigned common = lw < lh ? lw : lh, mask = (1u << common) - 1;
    if (lw >= lh) { *x = (bx & mask) | ((idx >> (2 * common)) << common); *y = by & mask; }
    else { *y = (by & mask) | ((idx >> (2 * common)) << common); *x = bx & mask; }
}
static uint32_t c565(uint16_t v)
{
    unsigned r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    return 0xFF000000u | ((b * 255 / 31) << 16) | ((g * 255 / 63) << 8) | (r * 255 / 31);
}
static uint32_t c1555(uint16_t v) { unsigned a = v >> 15 ? 255 : 0, r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31; return (a << 24) | ((b * 255 / 31) << 16) | ((g * 255 / 31) << 8) | (r * 255 / 31); }
static uint32_t cx555(uint16_t v) { return c1555(v) | 0xFF000000u; }
static uint32_t c4444(uint16_t v) { unsigned a = (v >> 12) & 15, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15; return ((a * 17u) << 24) | ((b * 17u) << 16) | ((g * 17u) << 8) | (r * 17u); }
static void dxt_block(const uint8_t *s, int fmt, uint32_t out[16])
{
    const uint8_t *cb = fmt == 0x0C ? s : s + 8;
    uint16_t c0 = cb[0] | (cb[1] << 8), c1 = cb[2] | (cb[3] << 8);
    uint32_t p[4] = { c565(c0), c565(c1), 0, 0 };
    if (c0 > c1 || fmt != 0x0C) {
        for (int i = 0; i < 3; ++i) { unsigned a = (p[0] >> (i * 8)) & 0xFF, b = (p[1] >> (i * 8)) & 0xFF; p[2] |= (((2 * a + b) / 3) << (i * 8)); p[3] |= (((a + 2 * b) / 3) << (i * 8)); }
        p[2] |= 0xFF000000u; p[3] |= 0xFF000000u;
    } else {
        for (int i = 0; i < 3; ++i) { unsigned a = (p[0] >> (i * 8)) & 0xFF, b = (p[1] >> (i * 8)) & 0xFF; p[2] |= (((a + b) / 2) << (i * 8)); }
        p[2] |= 0xFF000000u;
    }
    uint32_t bits = cb[4] | (cb[5] << 8) | (cb[6] << 16) | ((uint32_t)cb[7] << 24);
    for (int i = 0; i < 16; ++i) out[i] = p[(bits >> (i * 2)) & 3];
    if (fmt == 0x0E)
        for (int i = 0; i < 16; ++i) { unsigned a4 = (s[i / 2] >> ((i & 1) * 4)) & 0xF; out[i] = (out[i] & 0x00FFFFFFu) | ((a4 * 17u) << 24); }
    else if (fmt == 0x0F) {
        unsigned a0 = s[0], a1 = s[1]; uint64_t ab = 0;
        for (int i = 0; i < 6; ++i) ab |= (uint64_t)s[2 + i] << (i * 8);
        for (int i = 0; i < 16; ++i) {
            unsigned code = (ab >> (i * 3)) & 7, a;
            if (code == 0) a = a0; else if (code == 1) a = a1;
            else if (a0 > a1) a = ((8 - code) * a0 + (code - 1) * a1) / 7;
            else if (code == 6) a = 0; else if (code == 7) a = 255;
            else a = ((6 - code) * a0 + (code - 1) * a1) / 5;
            out[i] = (out[i] & 0x00FFFFFFu) | (a << 24);
        }
    }
}
static texcache_t *tex_get(uint32_t hdr)
{
    if (!hdr) return NULL;
    uint32_t fmtword = X_M32(hdr + 12);
    for (int i = 0; i < g_ntex; ++i) if (g_tex[i].hdr == hdr && g_tex[i].fmtword == fmtword) return &g_tex[i];
    unsigned fmt = (fmtword >> 8) & 0xFF;
    uint32_t sizeword = X_M32(hdr + 16);
    unsigned w, h, pitch;
    if (sizeword) { w = (sizeword & 0xFFF) + 1; h = ((sizeword >> 12) & 0xFFF) + 1; pitch = (((sizeword >> 24) & 0xFF) + 1) * 64; }
    else { w = 1u << ((fmtword >> 20) & 0xF); h = 1u << ((fmtword >> 24) & 0xF); pitch = 0; }
    if (!w || !h || w > 2048 || h > 2048) return NULL;
    const uint8_t *src = (const uint8_t *)X_G(0x80000000u | X_M32(hdr + 4));
    uint32_t *px = calloc((size_t)w * h, 4);
    if (fmt == 0x0C || fmt == 0x0E || fmt == 0x0F) {
        for (unsigned by = 0; by < h; by += 4)
            for (unsigned bx = 0; bx < w; bx += 4) {
                uint32_t blk[16]; dxt_block(src, fmt, blk);
                src += fmt == 0x0C ? 8 : 16;
                for (unsigned i = 0; i < 16; ++i) { unsigned x = bx + (i & 3), y = by + (i >> 2); if (x < w && y < h) px[y * w + x] = blk[i]; }
            }
    } else if (fmt == 0x12 || fmt == 0x1E || fmt == 0x3F || fmt == 0x40 || fmt == 0x41) {
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x) {
                const uint8_t *p = src + y * pitch + x * 4;
                uint32_t r, g, b, a;
                if (fmt == 0x3F) { a = p[3]; b = p[2]; g = p[1]; r = p[0]; }
                else if (fmt == 0x41) { r = p[3]; g = p[2]; b = p[1]; a = p[0]; }
                else if (fmt == 0x40) { b = p[3]; g = p[2]; r = p[1]; a = p[0]; }
                else { b = p[0]; g = p[1]; r = p[2]; a = fmt == 0x1E ? 255 : p[3]; }
                px[y * w + x] = (a << 24) | (b << 16) | (g << 8) | r;
            }
    } else if (fmt == 0x06 || fmt == 0x07) {
        for (unsigned i = 0; i < w * h; ++i) {
            unsigned x, y; unswizzle_xy(w, h, i, &x, &y);
            const uint8_t *p = src + i * 4;
            if (x < w && y < h) px[y * w + x] = ((fmt == 0x07 ? 255u : p[3]) << 24) | (p[0] << 16) | (p[1] << 8) | p[2];
        }
    } else if (fmt == 0x02 || fmt == 0x03 || fmt == 0x04 || fmt == 0x05) {              /* swizzled 16-bit */
        for (unsigned i = 0; i < w * h; ++i) {
            unsigned x, y; unswizzle_xy(w, h, i, &x, &y);
            uint16_t v = src[i * 2] | (src[i * 2 + 1] << 8);
            if (x < w && y < h) px[y * w + x] = fmt == 0x05 ? c565(v) : fmt == 0x04 ? c4444(v) : fmt == 0x02 ? c1555(v) : cx555(v);
        }
    } else if (fmt == 0x10 || fmt == 0x11 || fmt == 0x1C || fmt == 0x1D) {              /* linear 16-bit */
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x) {
                uint16_t v = src[y * pitch + x * 2] | (src[y * pitch + x * 2 + 1] << 8);
                px[y * w + x] = fmt == 0x11 ? c565(v) : fmt == 0x1D ? c4444(v) : fmt == 0x10 ? c1555(v) : cx555(v);
            }
    } else if (fmt == 0x00 || fmt == 0x0B || fmt == 0x13 || fmt == 0x19 || fmt == 0x1F || fmt == 0x01 || fmt == 0x1A || fmt == 0x1B || fmt == 0x20) {
        for (unsigned i = 0; i < w * h; ++i) {                                          /* 8/16-bit luminance-alpha-ish */
            unsigned bpp = (fmt == 0x1A || fmt == 0x20) ? 2 : 1;
            unsigned x, y;
            if (sizeword) { x = i % w; y = i / w; }
            else unswizzle_xy(w, h, i, &x, &y);
            const uint8_t *p = src + (sizeword ? y * pitch + x * bpp : i * bpp);
            uint8_t l = p[0], a = bpp == 2 ? p[1] : (fmt == 0x19 || fmt == 0x1F ? p[0] : 255);
            if (fmt == 0x19 || fmt == 0x1F) l = 255;
            if (x < w && y < h) px[y * w + x] = ((uint32_t)a << 24) | (l << 16) | (l << 8) | l;
        }
    } else {
        for (unsigned i = 0; i < w * h; ++i) px[i] = 0xFFFF00FFu;
        static unsigned warned; if (warned++ < 8) xk_os_log("[softgfx] unhandled texture format %02X (%ux%u)\n", fmt, w, h);
    }
    texcache_t *t = &g_tex[g_ntex < 512 ? g_ntex++ : 511];
    if (t->px) free(t->px);
    t->hdr = hdr; t->fmtword = fmtword; t->w = (int)w; t->h = (int)h; t->px = px;
    { unsigned rgbnz=0, anz=0; for (unsigned i=0;i<w*h;i++){ if(px[i]&0x00FFFFFF) rgbnz++; if(px[i]>>24) anz++; }
      t->alpha_only = (anz>16 && rgbnz*20 < anz); }
    if (getenv("XV_DUMP_TEX")) {
        char pth[128]; snprintf(pth, sizeof pth, "host/tex_%02X_%ux%u_%08X.ppm", fmt, w, h, hdr);
        FILE *f = fopen(pth, "wb");
        if (f) { fprintf(f, "P6\n%u %u\n255\n", w, h);
            for (unsigned i=0;i<w*h;i++){ uint8_t px3[3]={(uint8_t)px[i],(uint8_t)(px[i]>>8),(uint8_t)(px[i]>>16)}; fwrite(px3,1,3,f);} fclose(f); { char ap[140]; snprintf(ap,sizeof ap,"host/texA_%02X_%08X.pgm",fmt,hdr); FILE*fa=fopen(ap,"wb"); if(fa){fprintf(fa,"P5\n%u %u\n255\n",w,h); for(unsigned i=0;i<w*h;i++){uint8_t a=(uint8_t)(px[i]>>24); fwrite(&a,1,1,fa);} fclose(fa);} }
            xk_os_log("[softgfx] dumped %s (alpha sample %02X)\n", pth, (unsigned)(px[w*h/2]>>24)); }
    }
    return t;
}

/* ---- rasterizer ------------------------------------------------------------------------------- */
void xd3d_r_clear(uint32_t flags, uint32_t color, float z, uint32_t stencil)
{
    (void)z; (void)stencil;
    if (!(flags & 1)) return;
    uint32_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    uint32_t v = 0xFF000000u | (b << 16) | (g << 8) | r;
    for (int i = 0; i < W * H; ++i) g_fb[i] = v;
}

static void blend_px(int x, int y, float r, float g, float b, float a)
{
    if (x < 0 || y < 0 || x >= W || y >= H || a <= 0.003f) return;
    uint32_t *d = &g_fb[y * W + x];
    float dr = (*d & 0xFF), dg = ((*d >> 8) & 0xFF), db = ((*d >> 16) & 0xFF);
    float nr = r * 255.0f * a + dr * (1 - a), ng = g * 255.0f * a + dg * (1 - a), nb = b * 255.0f * a + db * (1 - a);
    *d = 0xFF000000u | ((uint32_t)(nb > 255 ? 255 : nb) << 16) | ((uint32_t)(ng > 255 ? 255 : ng) << 8) | (uint32_t)(nr > 255 ? 255 : nr);
}
static void sample(const texcache_t *t, float u, float v, float out[4])
{
    if (!t) { out[0] = out[1] = out[2] = out[3] = 1; return; }
    u -= floorf(u); v -= floorf(v);
    int x = (int)(u * (float)t->w), y = (int)(v * (float)t->h);
    if (x >= t->w) x = t->w - 1;
    if (y >= t->h) y = t->h - 1;
    uint32_t p = t->px[y * t->w + x];
    out[0] = (p & 0xFF) / 255.0f; out[1] = ((p >> 8) & 0xFF) / 255.0f; out[2] = ((p >> 16) & 0xFF) / 255.0f; out[3] = (p >> 24) / 255.0f;
}

typedef struct { float x, y, uv[4][2], c[4]; } svtx;
/* rasterise one triangle: texture t sampled with texcoord set ts; out = tex * vertex colour (src-alpha blend) */
static void tri(const texcache_t *t, int ts, const svtx *a, const svtx *b, const svtx *d)
{
    static int raster = -1; if (raster < 0) { const char *e = getenv("XV_SOFTGFX_RASTER"); raster = e ? atoi(e) != 0 : 1; }   /* XV_SOFTGFX_RASTER=0: keep the draw stream, skip pixels (profiling the guest scene) */
    if (!raster) return;
    float minx = fminf(a->x, fminf(b->x, d->x)), maxx = fmaxf(a->x, fmaxf(b->x, d->x));
    float miny = fminf(a->y, fminf(b->y, d->y)), maxy = fmaxf(a->y, fmaxf(b->y, d->y));
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
    float det = (b->x - a->x) * (d->y - a->y) - (d->x - a->x) * (b->y - a->y);
    if (fabsf(det) < 1e-6f) return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float l1 = ((px - a->x) * (d->y - a->y) - (d->x - a->x) * (py - a->y)) / det;
            float l2 = ((b->x - a->x) * (py - a->y) - (px - a->x) * (b->y - a->y)) / det;
            float l0 = 1.0f - l1 - l2;
            if (l0 < -0.001f || l1 < -0.001f || l2 < -0.001f) continue;
            float u = l0 * a->uv[ts][0] + l1 * b->uv[ts][0] + l2 * d->uv[ts][0], v = l0 * a->uv[ts][1] + l1 * b->uv[ts][1] + l2 * d->uv[ts][1];
            float tc[4]; sample(t, u, v, tc);
            if (t && t->alpha_only) { tc[0]=tc[1]=tc[2]=1.0f; }   /* font atlas: coverage in alpha, colour from vertex */
            float cr = l0 * a->c[0] + l1 * b->c[0] + l2 * d->c[0];
            float cg = l0 * a->c[1] + l1 * b->c[1] + l2 * d->c[1];
            float cb = l0 * a->c[2] + l1 * b->c[2] + l2 * d->c[2];
            float ca = l0 * a->c[3] + l1 * b->c[3] + l2 * d->c[3];
            blend_px(x, y, tc[0] * cr, tc[1] * cg, tc[2] * cb, tc[3] * ca);
        }
}

/* Halo's screen-space vertex programs for decl 0x1E13EC (v0 = xy, v4 = 2 floats, v9 = D3DCOLOR), by microcode FNV:
 *   1DAF0284 halo_vs_03 - widget: oT0/oT1 screen-space smoke scroll, oT2 = bitmap UV from v4 (all via c[-64..-58])
 *   4469E1F8 halo_vs_04 - text:   oT0 = v4 * c[-64]   (v4 = glyph atlas texel offset)
 *   BB2F446B halo_vs_38 - full-screen: oPos = v0 (clip space), oTn = dph(v4, c[-81+n*2..]) */
static float dph(const float v[4], const float c[4]) { return v[0] * c[0] + v[1] * c[1] + v[2] * c[2] + c[3]; }
static int ui_vs(const xd3d_im_vtx *in, svtx *out, uint32_t fnv)
{
    const float (*C)[4] = xd3d_state.vsc;
    float v0[4] = { in->a[0][0], in->a[0][1], 0.0f, 1.0f };
    float v4[4] = { in->a[4][0], in->a[4][1], 0.0f, 1.0f };
    memset(out->uv, 0, sizeof out->uv);
    if (fnv == 0xBB2F446Bu) {
        out->x = (v0[0] * 0.5f + 0.5f) * W; out->y = (0.5f - v0[1] * 0.5f) * H;
        for (int i = 0; i < 4; ++i) { out->uv[i][0] = dph(v4, C[15 + i * 2]); out->uv[i][1] = dph(v4, C[16 + i * 2]); }
    } else {
        float x = dph(v0, C[28]), y = dph(v0, C[29]), w = dph(v0, C[31]);
        if (w == 0.0f) w = 1.0f;
        out->x = (x / w * 0.5f + 0.5f) * W;
        out->y = (0.5f - y / w * 0.5f) * H;
        if (fnv == 0x4469E1F8u) { out->uv[0][0] = v4[0] * C[32][0]; out->uv[0][1] = v4[1] * C[32][1]; }
        else {
            /* oT0 = ((c34.x*v0 + v4*c34.y) * c37.xy + c35.zw) * c32.xy  (D3D reg = vsc index - 96) */
            float rx = (C[34][0] * v0[0] + v4[0] * C[34][1]) * C[37][0] + C[35][2];
            float ry = (C[34][0] * v0[1] + v4[1] * C[34][1]) * C[37][1] + C[35][3];
            out->uv[0][0] = rx * C[32][0]; out->uv[0][1] = ry * C[32][1];
            /* oT1 = ((c34.z*v0 + v4*c34.w) * c37.zw + c36.xy) * c33.xy */
            rx = (C[34][2] * v0[0] + v4[0] * C[34][3]) * C[37][2] + C[36][0];
            ry = (C[34][2] * v0[1] + v4[1] * C[34][3]) * C[37][3] + C[36][1];
            out->uv[1][0] = rx * C[33][0]; out->uv[1][1] = ry * C[33][1];
            /* oT2 = ((c35.x*v0 + v4*c35.y) * c38.xy + c36.zw) * c33.zw */
            rx = (C[35][0] * v0[0] + v4[0] * C[35][1]) * C[38][0] + C[36][2];
            ry = (C[35][0] * v0[1] + v4[1] * C[35][1]) * C[38][1] + C[36][3];
            out->uv[2][0] = rx * C[33][2]; out->uv[2][1] = ry * C[33][3];
        }
    }
    memcpy(out->c, in->a[9], 16);          /* v9 D3DCOLOR, already unpacked to RGBA */
    return 1;
}

/* Which texture stage carries the "image" for this combiner program: the highest tN read by any active
 * combiner stage (D3DPIXELSHADERDEF: AlphaInputs @0, RGBInputs @0x88, CombinerCount @0xD4; input byte low nibble
 * = register, 8..B = t0..t3).  Halo's 7-stage widget shader reads the bitmap from t2 and smoke from t0/t1;
 * the 1-stage text shader reads the glyph atlas from t0. */
static int ps_image_stage(uint32_t psdef)
{
    if (!psdef) return 0;
    const uint8_t *p = X_G(psdef);
    unsigned count = p[0xD4] & 0xF; if (count > 8) count = 8;
    int best = 0;
    for (unsigned st = 0; st < count; ++st)
        for (int k = 0; k < 4; ++k) {
            unsigned ra = p[st * 4 + k] & 0xF, rr = p[0x88 + st * 4 + k] & 0xF;
            if (ra >= 8 && ra <= 0xB && (int)(ra - 8) > best) best = (int)(ra - 8);
            if (rr >= 8 && rr <= 0xB && (int)(rr - 8) > best) best = (int)(rr - 8);
        }
    return best;
}

void xd3d_r_im_end(uint32_t prim, const xd3d_im_vtx *v, unsigned n)
{
    uint32_t vs = xd3d_state.vs_handle;
    uint32_t decl = (vs & 1) ? X_M32(vs & ~1u) : 0;
    { const char *df = getenv("XV_DUMP_DRAWS"); if (df && xd3d_frame() == (unsigned)atoi(df)) {
        xk_os_log("[dd] prim %u n %u decl %08X vs %08X tex0 %08X tex2 %08X ps %08X blend %u %03X/%03X at %u %03X %02X v0=(%.1f,%.1f) v4=(%.3f,%.3f) v9=(%.2f,%.2f,%.2f,%.2f) c[-68..]=%.2f %.2f\n",
            prim, n, decl, (vs & 1) ? X_M32((vs & ~1u) + 12) : 0, xd3d_state.texture[0], xd3d_state.texture[2], xd3d_state.ps_def,
            xd3d_state.alpha_blend, xd3d_state.src_blend, xd3d_state.dst_blend, xd3d_state.alpha_test, xd3d_state.alpha_func, xd3d_state.alpha_ref,
            n ? v[0].a[0][0] : 0.f, n ? v[0].a[0][1] : 0.f, n ? v[0].a[4][0] : 0.f, n ? v[0].a[4][1] : 0.f,
            n ? v[0].a[9][0] : 0.f, n ? v[0].a[9][1] : 0.f, n ? v[0].a[9][2] : 0.f, n ? v[0].a[9][3] : 0.f,
            xd3d_state.vsc[28][0], xd3d_state.vsc[34][0]);
        if (xd3d_state.ps_def) { const uint8_t *pd = X_G(xd3d_state.ps_def); uint32_t c0[8], c1[8], fc0, fc1; memcpy(c0, pd + 0x28, 32); memcpy(c1, pd + 0x48, 32); memcpy(&fc0, pd + 0xAC, 4); memcpy(&fc1, pd + 0xB0, 4);
            xk_os_log("[dd]   psc C0=%08X %08X %08X %08X %08X %08X %08X %08X C1=%08X %08X %08X %08X %08X %08X %08X %08X FC=%08X %08X count %u\n",
                      c0[0], c0[1], c0[2], c0[3], c0[4], c0[5], c0[6], c0[7], c1[0], c1[1], c1[2], c1[3], c1[4], c1[5], c1[6], c1[7], fc0, fc1, *(const uint32_t *)(pd + 0xD4)); }
        if (n >= 4 && v[0].a[9][3] > 0.5f) {
            for (int k = 32; k <= 38; ++k) xk_os_log("[dd]   c[%d] = %.4f %.4f %.4f %.4f\n", k - 96, xd3d_state.vsc[k][0], xd3d_state.vsc[k][1], xd3d_state.vsc[k][2], xd3d_state.vsc[k][3]);
            for (unsigned k = 0; k < n; ++k) xk_os_log("[dd]   v%u pos (%.1f,%.1f) uv (%.3f,%.3f)\n", k, v[k].a[0][0], v[k].a[0][1], v[k].a[4][0], v[k].a[4][1]);
        } } }
    { const char *df = getenv("XV_DUMP_PS"); if (df && xd3d_frame() == (unsigned)atoi(df) && xd3d_state.ps_def) {
        static uint32_t seen[8]; static int ns; int dup = 0; uint32_t hsh = 0x811C9DC5u; const uint8_t *pp = X_G(xd3d_state.ps_def);
        for (int i = 0; i < 0xF0; ++i) { hsh ^= pp[i]; hsh *= 0x01000193u; }
        for (int i = 0; i < ns; ++i) if (seen[i] == hsh) dup = 1;
        if (!dup && ns < 8) { seen[ns++] = hsh; char pth[96]; snprintf(pth, sizeof pth, "host/ps_%08X_%08X.bin", xd3d_state.ps_def, hsh);
            FILE *pf = fopen(pth, "wb"); if (pf) { fwrite(X_G(xd3d_state.ps_def), 1, 0xF0, pf); fclose(pf); xk_os_log("[dd] dumped psdef %08X hash %08X (tex0 %08X v9.a %.2f)\n", xd3d_state.ps_def, hsh, xd3d_state.texture[0], n ? v[0].a[9][3] : -1.f); } } } }
    if (prim != 7 || n < 4) return;
    if (decl != 0x001E13ECu) { static unsigned warned; if (warned++ < 6) xk_os_log("[softgfx] skipping im draw with decl %08X\n", decl); return; }
    uint32_t fnv = (vs & 1) ? X_M32((vs & ~1u) + 12) : 0;
    int ts = ps_image_stage(xd3d_state.ps_def);
    const texcache_t *t = tex_get(xd3d_state.texture[ts]);
    for (unsigned q = 0; q + 3 < n; q += 4) {
        svtx s[4];
        for (int i = 0; i < 4; ++i) ui_vs(&v[q + i], &s[i], fnv);
        if (getenv("XV_QUAD_Z")) { tri(t, ts, &s[0], &s[1], &s[2]); tri(t, ts, &s[1], &s[3], &s[2]); }
        else { tri(t, ts, &s[0], &s[1], &s[2]); tri(t, ts, &s[0], &s[2], &s[3]); }
    }
}

void xd3d_r_present(unsigned frame, unsigned draws)
{
    if (getenv("XV_PROBE_FONT") && (frame==30 || frame==120 || frame==400)) {
        uint32_t fd = 0x01EF8000u; const uint8_t *m=(const uint8_t*)X_G(0x80000000u|fd);
        unsigned nz=0; for (unsigned i=0;i<128*128;i++){ uint16_t v=m[i*2]|(m[i*2+1]<<8); if(((v>>12)&15)) nz++; }
        char pth[64]; snprintf(pth,sizeof pth,"host/font_f%u.pgm",frame); FILE*fa=fopen(pth,"wb");
        if(fa){ fprintf(fa,"P5\n128 128\n255\n"); for(unsigned i=0;i<128*128;i++){ uint16_t v=m[i*2]|(m[i*2+1]<<8); uint8_t a=((v>>12)&15)*17; fwrite(&a,1,1,fa);} fclose(fa);}
        xk_os_log("[probe] font atlas @f%u: %u/%u alpha-nonzero texels\n", frame, nz, 128*128);
    }
    { const char *md = getenv("XV_DUMP_MEM"); if (md && frame == (unsigned)atoi(md)) {
        FILE *mf = fopen("host/mem.bin", "wb"); if (mf) { fwrite(g_xram, 1, xk_mem_arena_size(), mf); fclose(mf); xk_os_log("[dd] dumped arena (%u MB)\n", xk_mem_arena_size() >> 20); } } }
    if (frame < 5 || frame % 120 == 0) xk_os_log("[softgfx] Present #%u (%u draws)\n", frame, draws);
    int every = 240;
    const char *e = getenv("XV_DUMP_EVERY");
    if (e) every = atoi(e);
    if (every > 0 && frame % (unsigned)every == 0 && g_dumped < 40) {
        char path[128]; snprintf(path, sizeof path, "host/softgfx_%04u.ppm", frame);
        FILE *f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", W, H);
            for (int i = 0; i < W * H; ++i) { uint8_t p[3] = { (uint8_t)g_fb[i], (uint8_t)(g_fb[i] >> 8), (uint8_t)(g_fb[i] >> 16) }; fwrite(p, 1, 3, f); }
            fclose(f); g_dumped++;
            xk_os_log("[softgfx] wrote %s\n", path);
        }
    }
}
