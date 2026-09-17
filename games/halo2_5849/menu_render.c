#include "menu_render.h"
#include "nv2a_vsh.h"
#include "menu_raster.h"
#include "menu_texture.h"
#include "menu_combiner.h"
#include "command_state.h"
#include "kelvin_clear.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern void xv_logf(const char *, ...);
extern uint64_t h2_graphics_time_us(void);

/* Phase profile since boot (microseconds), logged with the 500-draw stats line. */
static uint64_t prof_acquire_us, prof_transform_us, prof_raster_us, prof_draws;

/* The game's own zeta surface (Z24S8, pitch layout as the clear path requires)
 * mapped for depth testing; NULL when unbound or not that layout. */
static uint32_t *map_zeta(const h2_kelvin_clear *c, uint32_t W, uint32_t H, uint32_t *pitch_out)
{
    if (!c->has_zeta_dma || (c->format & 0xFFFFu) != 0x128u) return NULL;
    uint32_t pitch = c->pitch >> 16;
    if (!pitch || (pitch & 3) || (c->zeta_offset & 3) || (uint64_t)W * 4 > pitch) return NULL;
    uint64_t bytes = (uint64_t)(H - 1) * pitch + (uint64_t)W * 4;
    h2_dma_object dma;
    uint32_t phys;
    if (bytes > UINT32_MAX || !h2_dma_load(c->read_instance, c->opaque, c->dma_zeta, &dma) ||
        !h2_dma_resolve(&dma, c->zeta_offset, (uint32_t)bytes, 1, c->physical_bytes, &phys)) return NULL;
    *pitch_out = pitch;
    return c->map_physical(c->opaque, phys, (uint32_t)bytes);
}

/* Diagnostic knobs from env.txt (delivered by boot.c): XV_MENU_DEPTH=0 disables
 * the depth test, XV_MENU_DETAIL=N logs the first N draws in full. */
static int knob(const char *name, int dflt)
{
    const char *v = getenv(name);
    return v && *v ? atoi(v) : dflt;
}

/* Private diagnostic: dump the composited back buffer so the rendered menu can
 * be inspected offline even when the frame never reaches the display flip. */
void h2_menu_dump_target(const uint8_t *target, uint32_t W, uint32_t H, uint64_t drawn, uint32_t color_offset);
static void dump_backbuffer(const uint8_t *target, uint32_t W, uint32_t H, uint64_t drawn,
                            uint32_t color_offset)
{
    h2_menu_dump_target(target, W, H, drawn, color_offset);
}
void h2_menu_dump_target(const uint8_t *target, uint32_t W, uint32_t H, uint64_t drawn, uint32_t color_offset)
{
    /* One file per render target: the menu draws the backdrop and the UI layer
     * into different colour buffers, so a single file would only ever hold the
     * target of the most recent draw. */
    char path[96];
    snprintf(path, sizeof path, "ux0:data/xita-halo2/menu-frame-%08X.bin", color_offset);
    FILE *fp = fopen(path, "wb");
    if (!fp) return;
    uint32_t hdr[4] = {W, H, W * 4, (uint32_t)drawn};
    int ok = fwrite(hdr, 1, sizeof hdr, fp) == sizeof hdr &&
             fwrite(target, 1, (size_t)W * H * 4, fp) == (size_t)W * H * 4;
    if (fclose(fp) == 0 && ok)
        xv_logf("[h2/menu-render] private back-buffer dump %s W=%u H=%u after draw=%llu\n",
                path, W, H, (unsigned long long)drawn);
}

#define MAX_VERTS 65536u   /* menu item geometry uses large indexed draws (16-bit indices) */

typedef struct { const uint8_t *base; uint32_t stride, type, count; int enabled; } vattr;

static float rd_f32(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
static int rd_s16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static uint32_t rd_u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static uint32_t attr_bytes(uint32_t type, uint32_t count)
{
    switch (type) {
    case 2: return count * 4;          /* float */
    case 1: case 5: return count * 2;  /* signed short */
    case 0: case 4: return count;      /* unsigned bytes */
    default: return 4;                 /* packed */
    }
}

static void decode_attr(const vattr *a, const uint8_t *p, float out[4])
{
    out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f;
    if (!a->enabled || !p) return;
    unsigned n = a->count < 4 ? a->count : 4;
    switch (a->type) {
    case 2: for (unsigned k = 0; k < n; ++k) out[k] = rd_f32(p + k * 4); break;
    case 1: for (unsigned k = 0; k < n; ++k) out[k] = rd_s16(p + k * 2) / 32767.0f; break;
    case 5: for (unsigned k = 0; k < n; ++k) out[k] = (float)rd_s16(p + k * 2); break;
    case 0: if (n == 4) { out[0] = p[2] / 255.0f; out[1] = p[1] / 255.0f; out[2] = p[0] / 255.0f; out[3] = p[3] / 255.0f; } /* BGRA */
            else for (unsigned k = 0; k < n; ++k) out[k] = p[k] / 255.0f;               /* 1-3 unsigned bytes */
            break;
    case 4: for (unsigned k = 0; k < n; ++k) out[k] = p[k] / 255.0f; break; /* RGBA / 1-3 unsigned bytes */
    case 6: { uint32_t w = rd_u32(p);                                       /* packed 11/11/10 signed */
              int x = (int)(w << 21) >> 21, y = (int)(w << 10) >> 21, z = (int)w >> 22;
              out[0] = x / 1023.0f; out[1] = y / 1023.0f; out[2] = z / 511.0f; } break;
    default: break;
    }
}

static void resolve_arrays(const h2_command_state *s, const h2_kelvin_clear *c,
                            vattr attrs[16], uint32_t maxv)
{
    for (unsigned a = 0; a < 16; ++a) {
        attrs[a].enabled = 0; attrs[a].base = NULL;
        uint32_t fmt = s->setup[(0x1760 + a * 4) / 4];
        uint32_t type = fmt & 0xF, count = (fmt >> 4) & 0xF, stride = (fmt >> 8) & 0xFFFFFF;
        if (count == 0) continue;
        uint32_t off = s->setup[(0x1720 + a * 4) / 4];
        unsigned dmab = off >> 31;
        uint32_t byteoff = off & 0x7FFFFFFF, inst = s->dma[dmab ? 8 : 7];
        if (!inst) continue;
        h2_dma_object obj;
        if (!h2_dma_load(c->read_instance, c->opaque, inst, &obj)) continue;
        uint32_t span = (uint64_t)(maxv + 1) * stride + attr_bytes(type, count) > 0x08000000u
                        ? 0 : (maxv + 1) * stride + attr_bytes(type, count);
        uint32_t phys;
        if (!span || !h2_dma_resolve(&obj, byteoff, span, 0, c->physical_bytes, &phys)) continue;
        const uint8_t *p = c->map_physical(c->opaque, phys, span);
        if (!p) continue;
        attrs[a].base = p; attrs[a].stride = stride; attrs[a].type = type;
        attrs[a].count = count; attrs[a].enabled = 1;
    }
}

static void transform_out(const float o[NV2A_VSH_OUTPUTS][4], menu_vertex_out *v)
{
    v->x = o[NV2A_O_POS][0];
    v->y = o[NV2A_O_POS][1];
    v->z = o[NV2A_O_POS][2] / 16777215.0f;
    v->w = o[NV2A_O_POS][3];
    for (unsigned k = 0; k < 4; ++k) v->color[k] = o[NV2A_O_D0][k];
    for (unsigned k = 0; k < 4; ++k) v->specular[k] = o[NV2A_O_D1][k];
    for (unsigned t = 0; t < 4; ++t) {
        v->uv[t][0] = o[NV2A_O_T0 + t][0];
        v->uv[t][1] = o[NV2A_O_T0 + t][1];
    }
}

static menu_vertex_out g_verts[MAX_VERTS];

static uint32_t sel(const uint16_t *indices, uint32_t base, uint32_t i)
{ return indices ? indices[i] : base + i; }

static void one_tri(const menu_raster_state *rs, uint32_t n, uint32_t a, uint32_t b, uint32_t c)
{ if (a < n && b < n && c < n) menu_raster_triangle(rs, &g_verts[a], &g_verts[b], &g_verts[c]); }

static void assemble(const menu_raster_state *rs, uint16_t prim, uint32_t n,
                      const uint16_t *indices, uint32_t count, uint32_t base)
{
    if (count < 3) return;
    switch (prim) {
    case 5: for (uint32_t i = 0; i + 2 < count; i += 3) {
                one_tri(rs, n, sel(indices, base, i), sel(indices, base, i + 1), sel(indices, base, i + 2));
            } break;
    case 6: for (uint32_t i = 0; i + 2 < count; ++i) {
                uint32_t a = sel(indices, base, i), b = sel(indices, base, i + 1), d = sel(indices, base, i + 2);
                if (i & 1) one_tri(rs, n, b, a, d); else one_tri(rs, n, a, b, d);
            } break;
    case 7: for (uint32_t i = 1; i + 1 < count; ++i) {
                one_tri(rs, n, sel(indices, base, 0), sel(indices, base, i), sel(indices, base, i + 1));
            } break;
    case 8: for (uint32_t i = 0; i + 3 < count; i += 4) {
                uint32_t a = sel(indices, base, i), b = sel(indices, base, i + 1),
                         d = sel(indices, base, i + 2), e = sel(indices, base, i + 3);
                one_tri(rs, n, a, b, d); one_tri(rs, n, a, d, e);
            } break;
    default: break;
    }
}

static int software_body(void *opaque, const h2_menu_request *r, uint8_t *ab_before, uint8_t *ab_gpu);

/* A/B helper: the software draw over the restored buffer, then the comparison against the GXM result. */
int h2_menu_software_draw_ab(void *opaque, const h2_menu_request *r, uint8_t *before, uint8_t *gpu)
{ return software_body(opaque, r, before, gpu); }

int h2_menu_software_render(void *opaque, const h2_menu_request *r)
{
    (void)opaque;
    const h2_command_state *s = r->state;
    const h2_kelvin_clear *c = r->clear;
    if (!s || !c || !c->map_physical || !c->read_instance || !c->has_color_dma) return 0;
    if (!s->program_load) return 0;
    /* XV_MENU_GXM=1 routes draws to the GXM backend (menu_gxm.c); a draw it cannot take
     * (no compiled shader pair yet) falls through to this software path after the GXM
     * backend has flushed its pending scene, so both paths always see coherent buffers. */
    static int gxm_knob = -1, ab_every = -1;
    if (gxm_knob < 0) { gxm_knob = knob("XV_MENU_GXM", 0); ab_every = knob("XV_MENU_GXM_AB", 0); }
    if (gxm_knob) {
        extern int h2_menu_gxm_render(void *opaque, const h2_menu_request *r);
        extern void h2_menu_gxm_flush(void);
        static uint64_t ab_serial;
        int ab = ab_every > 0 && !(++ab_serial % (uint64_t)ab_every) && r->primitive != 0;
        uint8_t *ab_before = NULL, *ab_gpu = NULL;
        uint32_t abW = c->clip_horizontal >> 16, abH = c->clip_vertical >> 16;
        if (ab && abW == 640 && abH == 480) {
            /* A/B fidelity probe: keep a copy of the target, draw with GXM (flushed so the guest
             * buffer holds its result), keep that, restore the copy, draw with the software
             * rasterizer, compare, then put the GXM result back as the state going forward. */
            h2_dma_object dmab; uint32_t bphys;
            if (h2_dma_load(c->read_instance, c->opaque, c->dma_color, &dmab) &&
                h2_dma_resolve(&dmab, c->color_offset, 640 * 480 * 4, 1, c->physical_bytes, &bphys)) {
                uint8_t *guest = c->map_physical(c->opaque, bphys, 640 * 480 * 4);
                if (guest) {
                    h2_menu_gxm_flush();
                    ab_before = malloc(640 * 480 * 4); ab_gpu = malloc(640 * 480 * 4);
                    if (ab_before && ab_gpu) memcpy(ab_before, guest, 640 * 480 * 4);
                    else { free(ab_before); free(ab_gpu); ab_before = ab_gpu = NULL; }
                    if (ab_before) {
                        int result = h2_menu_gxm_render(opaque, r);
                        h2_menu_gxm_flush();
                        if (result == 1) {
                            memcpy(ab_gpu, guest, 640 * 480 * 4);
                            memcpy(guest, ab_before, 640 * 480 * 4);
                            /* fall through: software draw into the restored buffer, compared below */
                        } else { free(ab_before); free(ab_gpu); ab_before = ab_gpu = NULL; if (result != -1) return result; }
                    }
                }
            }
        }
        if (!ab_before) {
            int result = h2_menu_gxm_render(opaque, r);
            if (result != -1) return result;                 /* 1 drawn, 0 rejected; -1 = fall back */
        }
        if (ab_before) {
            extern int h2_menu_software_draw_ab(void *opaque, const h2_menu_request *r, uint8_t *before, uint8_t *gpu);
            return h2_menu_software_draw_ab(opaque, r, ab_before, ab_gpu);
        }
    }

    return software_body(opaque, r, NULL, NULL);
}

static int software_body(void *opaque, const h2_menu_request *r, uint8_t *ab_before, uint8_t *ab_gpu)
{
    (void)opaque;
    const h2_command_state *s = r->state;
    const h2_kelvin_clear *c = r->clear;
    uint32_t W = c->clip_horizontal >> 16, H = c->clip_vertical >> 16;
    if (W < 16 || W > 2048 || H < 16 || H > 2048) { free(ab_before); free(ab_gpu); return 0; }
    const uint32_t bytes = W * H * 4;

    h2_dma_object dmac;
    uint32_t cphys;
    if (!h2_dma_load(c->read_instance, c->opaque, c->dma_color, &dmac) ||
        !h2_dma_resolve(&dmac, c->color_offset, bytes, 1, c->physical_bytes, &cphys)) { free(ab_before); free(ab_gpu); return 0; }
    uint8_t *target = c->map_physical(c->opaque, cphys, bytes);
    if (!target) { free(ab_before); free(ab_gpu); return 0; }

    menu_raster_state rs;
    memset(&rs, 0, sizeof rs);
    rs.target.pixels = target; rs.target.width = W; rs.target.height = H; rs.target.pitch = W * 4;
    rs.clip_x0 = 0; rs.clip_y0 = 0; rs.clip_x1 = (int32_t)W - 1; rs.clip_y1 = (int32_t)H - 1;

    /* Decoded textures come from the content-hashed cache (menu_texture.c); the
     * menu binds up to 640x480 linear screen images and 512x512 materials, so
     * allow 1024x1024 per unit. The draw serial pins this draw's entries. */
    enum { TEX_CAP = 1024 * 1024 };
    static uint64_t drawn;
    int textured = 0;
    uint64_t t_start = h2_graphics_time_us();
    for (unsigned u = 0; u < 4; ++u) {
        uint32_t tw = 0, th = 0;
        int linear = 0;
        const uint32_t *px = menu_texture_acquire(s, c, u, drawn + 1, TEX_CAP, &tw, &th, &linear, NULL);
        if (px) {
            rs.tex[u].texels = px; rs.tex[u].width = tw; rs.tex[u].height = th;
            rs.tex[u].texel_coords = linear;
            textured = 1;
        }
    }

    /* Captured NV2A fragment/ROP state: blend, alpha test, depth test. */
    static int unsupported_blend_logged;
    if (s->setup[0x304 / 4]) {
        rs.sfactor = s->setup[0x344 / 4]; rs.dfactor = s->setup[0x348 / 4];
        rs.equation = s->setup[0x350 / 4]; rs.blend_color = s->setup[0x34C / 4];
        if (menu_raster_blend_supported(rs.sfactor, rs.dfactor, rs.equation)) {
            rs.blend = MENU_BLEND_FUNC;
        } else {
            rs.blend = MENU_BLEND_ALPHA;
            if (!unsupported_blend_logged++)
                xv_logf("[h2/menu-render] unsupported blend sfactor=%04X dfactor=%04X equation=%04X; using src-alpha\n",
                        rs.sfactor, rs.dfactor, rs.equation);
        }
    } else {
        rs.blend = MENU_BLEND_OPAQUE;
    }
    rs.alpha_test = s->setup[0x300 / 4] & 1;
    rs.alpha_func = s->setup[0x33C / 4];
    rs.alpha_ref = (float)(s->setup[0x340 / 4] & 0xFF) / 255.0f;
    static int depth_knob = -1, pool_knob = -1;
    if (depth_knob < 0) depth_knob = knob("XV_MENU_DEPTH", 1);
    if (pool_knob < 0) {
        extern void h2_menu_render_tick(void);
        pool_knob = knob("XV_MENU_THREADS", 2); menu_raster_pool_install(pool_knob);
        menu_raster_tick = h2_menu_render_tick;
    }
    if (depth_knob && s->setup[0x30C / 4]) {
        uint32_t zpitch = 0;
        rs.depth.pixels = map_zeta(c, W, H, &zpitch);
        rs.depth.width = W; rs.depth.height = H; rs.depth.pitch = zpitch;
        rs.depth.func = s->setup[0x354 / 4];
        rs.depth.write = s->setup[0x35C / 4] & 1;
    }
    static menu_combiner cb;
    menu_combiner_decode(s, &cb);
#ifdef H2_MENU_NOCOMBINER
    rs.combiner = NULL;                 /* fast diffuse-only path for input/progression testing */
#else
    rs.combiner = &cb;
#endif

    uint64_t t_tex = h2_graphics_time_us();
    prof_acquire_us += t_tex - t_start;
    uint32_t n = 0;
#define n_for_ab n
    if (r->vertex_count) {                         /* immediate vertices */
        n = r->vertex_count < MAX_VERTS ? r->vertex_count : MAX_VERTS;
        for (uint32_t i = 0; i < n; ++i) {
            float o[NV2A_VSH_OUTPUTS][4];
            nv2a_vsh_run(s->program, s->program_start, s->program_load,
                         r->vertices[i].attribute, (const float (*)[4])s->constants, o);
            transform_out(o, &g_verts[i]);
        }
        uint64_t t_xf = h2_graphics_time_us(); prof_transform_us += t_xf - t_tex;
        assemble(&rs, r->primitive, n, NULL, n, 0);
        prof_raster_us += h2_graphics_time_us() - t_xf;
    } else {                                       /* indexed / array vertices */
        uint32_t maxv = 0, lo = 0, count;
        const uint16_t *indices = NULL;
        if (r->index_count) {
            for (uint32_t i = 0; i < r->index_count; ++i) if (r->indices[i] > maxv) maxv = r->indices[i];
            indices = r->indices; count = r->index_count;
        } else if (r->array_count) {
            maxv = r->array_start + r->array_count - 1; lo = r->array_start; count = r->array_count;
        } else {
            free(ab_before); free(ab_gpu); return 1; /* BEGIN/END with no emission draws nothing; still a valid draw */
        }
        if (maxv + 1 > MAX_VERTS) { free(ab_before); free(ab_gpu); return 0; }
        vattr attrs[16];
        resolve_arrays(s, c, attrs, maxv);
        n = maxv + 1;
        for (uint32_t vi = lo; vi <= maxv; ++vi) {
            float in[16][4], o[NV2A_VSH_OUTPUTS][4];
            for (unsigned a = 0; a < 16; ++a)
                decode_attr(&attrs[a], attrs[a].enabled ? attrs[a].base + (uint64_t)vi * attrs[a].stride : NULL, in[a]);
            nv2a_vsh_run(s->program, s->program_start, s->program_load, in, (const float (*)[4])s->constants, o);
            transform_out(o, &g_verts[vi]);
        }
        uint64_t t_xf = h2_graphics_time_us(); prof_transform_us += t_xf - t_tex;
        assemble(&rs, r->primitive, n, indices, count, lo);
        prof_raster_us += h2_graphics_time_us() - t_xf;
    }
    ++prof_draws;

    static int detail = -1;
    if (detail < 0) detail = knob("XV_MENU_DETAIL", 40);
    if (drawn < (uint64_t)detail || !(drawn % 200)) {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (uint32_t i = 0; i < n; ++i) {
            const menu_vertex_out *v = &g_verts[i];
            if (v->x < x0) x0 = v->x;
            if (v->x > x1) x1 = v->x;
            if (v->y < y0) y0 = v->y;
            if (v->y > y1) y1 = v->y;
        }
        char tex[96] = ""; size_t used = 0;
        for (unsigned u = 0; u < 4; ++u) {
            uint32_t code = 0, tw = 0, th = 0;
            int en = menu_texture_describe(s, u, &code, &tw, &th);
            if (en) used += (size_t)snprintf(tex + used, sizeof tex - used, " t%u=%02X/%ux%u%s", u, code, tw, th,
                                              rs.tex[u].texels ? "" : "!");
        }
        const menu_vertex_out *v0 = &g_verts[0];
        xv_logf("[h2/menu-render] draw=%llu prim=%u verts=%u W=%u H=%u textured=%d color=%08X bbox=%.0f,%.0f-%.0f,%.0f "
                "v0=rgba(%.2f,%.2f,%.2f,%.2f)uv(%.2f,%.2f)z=%.3f blend=%d/%04X/%04X/%04X atest=%d/%03X/%.2f depth=%s/%03X/%d%s\n",
                (unsigned long long)drawn, r->primitive, n, W, H, textured, c->color_offset, x0, y0, x1, y1,
                v0->color[0], v0->color[1], v0->color[2], v0->color[3], v0->uv[0][0], v0->uv[0][1], v0->z,
                rs.blend, rs.sfactor, rs.dfactor, rs.equation, rs.alpha_test, rs.alpha_func, rs.alpha_ref,
                rs.depth.pixels ? "on" : (s->setup[0x30C / 4] ? "unmapped" : "off"), rs.depth.func, rs.depth.write, tex);
    }
    /* Text diagnostic: the first few depth-off 2D quads that sample a linear image
     * (the font cache) get their decoded unit-0 image dumped privately plus the
     * combiner/texture registers logged, to see what the glyph quads really sample. */
    static int text_dumps;
    static unsigned text_quads;
    if (r->primitive == 7 && r->vertex_count && !s->setup[0x30C / 4] && rs.tex[0].texels &&
        rs.tex[0].texel_coords && text_dumps < 8 && (text_quads++ % 400) < 2) {
        char path[96];
        snprintf(path, sizeof path, "ux0:data/xita-halo2/menu-tex0-%d.bin", text_dumps);
        FILE *fp = fopen(path, "wb");
        if (fp) {
            uint32_t hdr[4] = {rs.tex[0].width, rs.tex[0].height, rs.tex[0].width * 4, (uint32_t)drawn};
            fwrite(hdr, 1, sizeof hdr, fp);
            fwrite(rs.tex[0].texels, 4, (size_t)rs.tex[0].width * rs.tex[0].height, fp);
            fclose(fp);
        }
        xv_logf("[h2/menu-render] text quad draw=%llu unit0 off=%08X fmt=%08X addr=%08X ctrl0=%08X ctrl1=%08X filter=%08X rect=%08X "
                "-> %s; combiner stages=%u rgb_in0=%08X rgb_out0=%08X alpha_in0=%08X alpha_out0=%08X "
                "rgb_in1=%08X alpha_in1=%08X final_abcd=%08X final_efg=%08X c0=%08X c1=%08X\n",
                (unsigned long long)drawn, s->setup[0x1B00 / 4], s->setup[0x1B04 / 4], s->setup[0x1B08 / 4],
                s->setup[0x1B0C / 4], s->setup[0x1B10 / 4], s->setup[0x1B14 / 4], s->setup[0x1B1C / 4], path,
                cb.stages, cb.rgb_in[0], cb.rgb_out[0], cb.alpha_in[0], cb.alpha_out[0], cb.rgb_in[1], cb.alpha_in[1],
                cb.final_abcd, cb.final_efg, cb.factor0[0], cb.factor1[0]);
        for (uint32_t i = 0; i < n && i < 4; ++i)
            xv_logf("[h2/menu-render]   v%u xy=(%.1f,%.1f) rgba=(%.2f,%.2f,%.2f,%.2f) uv0=(%.2f,%.2f) uv1=(%.2f,%.2f)\n",
                    i, g_verts[i].x, g_verts[i].y, g_verts[i].color[0], g_verts[i].color[1], g_verts[i].color[2],
                    g_verts[i].color[3], g_verts[i].uv[0][0], g_verts[i].uv[0][1], g_verts[i].uv[1][0], g_verts[i].uv[1][1]);
        ++text_dumps;
    }
    /* Post-process diagnostic: the first few full-screen quads (screen-space passes
     * that sample the 640x480 scene image) get their complete combiner state logged,
     * since they decide the presented frame's tone. */
    static int fullscreen_dumps, fullscreen_seen;
    if (n >= 4 && n <= 6 && (r->primitive == 6 || r->primitive == 7 || r->primitive == 8)) {
        float fx0 = 1e9f, fy0 = 1e9f, fx1 = -1e9f, fy1 = -1e9f;
        for (uint32_t i = 0; i < n; ++i) {
            if (g_verts[i].x < fx0) fx0 = g_verts[i].x;
            if (g_verts[i].x > fx1) fx1 = g_verts[i].x;
            if (g_verts[i].y < fy0) fy0 = g_verts[i].y;
            if (g_verts[i].y > fy1) fy1 = g_verts[i].y;
        }
        /* Log the first three full-screen passes, then every 60th (fade level over time). */
        if (fx0 <= 1.0f && fy0 <= 1.0f && fx1 >= (float)W - 1.0f && fy1 >= (float)H - 1.0f &&
            (fullscreen_dumps < 3 || !(++fullscreen_seen % 60))) {
            char tex[128] = ""; size_t used = 0;
            for (unsigned u = 0; u < 4; ++u) {
                uint32_t code = 0, tw = 0, th = 0;
                if (menu_texture_describe(s, u, &code, &tw, &th))
                    used += (size_t)snprintf(tex + used, sizeof tex - used, " t%u=%02X/%ux%u@%08X%s", u, code, tw, th,
                                              s->setup[(0x1B00 + u * 64) / 4], rs.tex[u].texels ? "" : "!");
            }
            xv_logf("[h2/menu-render] fullscreen draw=%llu prim=%u color=%08X blend=%d/%04X/%04X/%04X stages=%u final_abcd=%08X final_efg=%08X final_c0=%08X final_c1=%08X%s\n",
                    (unsigned long long)drawn, r->primitive, c->color_offset, rs.blend, rs.sfactor, rs.dfactor, rs.equation,
                    cb.stages, cb.final_abcd, cb.final_efg, cb.final_factor0, cb.final_factor1, tex);
            for (unsigned i = 0; i < cb.stages && i < 8; ++i)
                xv_logf("[h2/menu-render]   stage%u rgb_in=%08X rgb_out=%08X alpha_in=%08X alpha_out=%08X c0=%08X c1=%08X\n",
                        i, cb.rgb_in[i], cb.rgb_out[i], cb.alpha_in[i], cb.alpha_out[i], cb.factor0[i], cb.factor1[i]);
            xv_logf("[h2/menu-render]   v0 rgba=(%.2f,%.2f,%.2f,%.2f) uv0=(%.1f,%.1f) uv1=(%.1f,%.1f) uv2=(%.1f,%.1f) uv3=(%.1f,%.1f)\n",
                    g_verts[0].color[0], g_verts[0].color[1], g_verts[0].color[2], g_verts[0].color[3],
                    g_verts[0].uv[0][0], g_verts[0].uv[0][1], g_verts[0].uv[1][0], g_verts[0].uv[1][1],
                    g_verts[0].uv[2][0], g_verts[0].uv[2][1], g_verts[0].uv[3][0], g_verts[0].uv[3][1]);
            ++fullscreen_dumps;
        }
    }
    /* Shader inventory for the GXM backend: every distinct (vertex program, combiner)
     * pair the menu draws with is logged once and its microcode dumped privately, so
     * the offline pipeline (tools/ps_pipeline.py + shadercomp) can produce the .gxp set. */
    {
        static uint64_t seen_vp[256], seen_pair[512];
        static unsigned n_vp, n_pair;
        uint64_t hv = 0xCBF29CE484222325ull ^ s->program_start, hp = 0x84222325CBF29CE4ull ^ cb.stages;
        for (uint32_t i = 0; i < s->program_load && i < 136; ++i)
            for (unsigned k = 0; k < 4; ++k) { hv ^= s->program[i][k]; hv *= 0x100000001B3ull; }
        for (unsigned i = 0; i < cb.stages && i < 8; ++i) {
            uint32_t w[4] = {cb.rgb_in[i], cb.rgb_out[i], cb.alpha_in[i], cb.alpha_out[i]};
            for (unsigned k = 0; k < 4; ++k) { hp ^= w[k]; hp *= 0x100000001B3ull; }
        }
        hp ^= cb.final_abcd; hp *= 0x100000001B3ull; hp ^= cb.final_efg; hp *= 0x100000001B3ull;
        hp ^= s->setup[0x1E70 / 4]; hp *= 0x100000001B3ull;          /* shader stage programs */
        uint64_t pair = hv * 0x9E3779B97F4A7C15ull ^ hp;
        unsigned i;
        for (i = 0; i < n_pair && seen_pair[i] != pair; ++i) {}
        if (i == n_pair && n_pair < 512) {
            seen_pair[n_pair++] = pair;
            unsigned j;
            for (j = 0; j < n_vp && seen_vp[j] != hv; ++j) {}
            if (j == n_vp && n_vp < 256) {
                seen_vp[n_vp++] = hv;
                char path[96];
                snprintf(path, sizeof path, "ux0:data/xita-halo2/menu-vp-%016llx.bin", (unsigned long long)hv);
                FILE *fp = fopen(path, "wb");
                if (fp) { fwrite(&s->program_start, 4, 1, fp); fwrite(&s->program_load, 4, 1, fp);
                          fwrite(s->program, 16, s->program_load < 136 ? s->program_load : 136, fp); fclose(fp); }
            }
            char cw[560]; size_t used = 0;
            for (unsigned k = 0; k < cb.stages && k < 8; ++k)
                used += (size_t)snprintf(cw + used, sizeof cw - used, " %08X/%08X/%08X/%08X/%08X/%08X", cb.rgb_in[k], cb.rgb_out[k],
                                         cb.alpha_in[k], cb.alpha_out[k], cb.factor0[k], cb.factor1[k]);
            used += (size_t)snprintf(cw + used, sizeof cw - used, " attrs=");
            for (unsigned k = 0; k < 16; ++k)          /* vertex array formats: the GXM attribute layout per VP */
                used += (size_t)snprintf(cw + used, sizeof cw - used, "%s%X", k ? "," : "", s->setup[(0x1760 + k * 4) / 4] & 0xFF);
            /* ctl = SET_COMBINER_CONTROL (0x1E60), cmp = SET_SHADER_CLIP_PLANE_MODE/compare (0x1E6C),
             * dot = SET_DOT_RGBMAPPING (0x1E74), inp = SET_SHADER_OTHER_STAGE_INPUT (0x1E78):
             * with the stage modes (0x1E70) these complete a D3DPIXELSHADERDEF for the generator. */
            xv_logf("[h2/menu-shader] pair#%u vp=%016llx ps=%016llx prim=%u vp_len=%u vp_start=%u stages=%u final=%08X/%08X/%08X/%08X stagectl=%08X ctl=%08X cmp=%08X dot=%08X inp=%08X tex=%X%s\n",
                    n_pair, (unsigned long long)hv, (unsigned long long)hp, r->primitive, s->program_load, s->program_start, cb.stages,
                    cb.final_abcd, cb.final_efg, cb.final_factor0, cb.final_factor1, s->setup[0x1E70 / 4],
                    s->setup[0x1E60 / 4], s->setup[0x1E6C / 4], s->setup[0x1E74 / 4], s->setup[0x1E78 / 4], cb.tex_used, cw);
        }
    }
    if (ab_before && ab_gpu && bytes == 640 * 480 * 4) {
        /* compare the software result (target) with the GXM result (ab_gpu) over the pixels either path touched */
        uint64_t sum = 0, n = 0, worst = 0;
        for (uint32_t i = 0; i < 640 * 480; ++i) {
            const uint8_t *p = target + i * 4, *g = ab_gpu + i * 4, *b = ab_before + i * 4;
            int touched = memcmp(p, b, 3) || memcmp(g, b, 3);
            if (!touched) continue;
            unsigned d = 0;
            for (unsigned k = 0; k < 3; ++k) { int e = (int)p[k] - (int)g[k]; d += (unsigned)(e < 0 ? -e : e); }
            sum += d; ++n; if (d > worst) worst = d;
        }
        char tex[96] = ""; size_t used = 0;
        for (unsigned u = 0; u < 4; ++u) {
            uint32_t code = 0, tw = 0, th = 0;
            if (menu_texture_describe(s, u, &code, &tw, &th))
                used += (size_t)snprintf(tex + used, sizeof tex - used, " t%u=%02X/%ux%u", u, code, tw, th);
        }
        /* sceClibPrintf: no floating point, and 64-bit arguments after the first misalign the rest of
         * the list - keep every argument 32-bit (mean as integer hundredths). */
        unsigned mean_c = n ? (unsigned)((sum * 100ull) / (n * 3ull)) : 0;
        xv_logf("[h2/menu-ab] draw=%u prim=%u verts=%u touched=%u mean|diff|=%u.%02u worst=%u blend=%d/%04X/%04X depth=%s stages=%u vp_len=%u%s\n",
                (unsigned)drawn, (unsigned)r->primitive, (unsigned)n_for_ab, (unsigned)n, mean_c / 100, mean_c % 100,
                (unsigned)worst, (int)rs.blend, (unsigned)rs.sfactor, (unsigned)rs.dfactor, rs.depth.pixels ? "on" : "off",
                (unsigned)cb.stages, (unsigned)s->program_load, tex);
        memcpy(target, ab_gpu, 640 * 480 * 4);           /* the GXM result is the state going forward */
        free(ab_before); free(ab_gpu); ab_before = ab_gpu = NULL;
    }
    ++drawn;
    if (!(drawn % 500)) {
        char stats[512];
        uint64_t hits = 0, misses = 0; size_t bytes = 0;
        menu_texture_stats(stats, sizeof stats);
        menu_texture_cache_stats(&hits, &misses, &bytes);
        xv_logf("[h2/menu-render] texture formats ok/unsupported/toolarge/nomap: %s; cache hits=%llu misses=%llu bytes=%lu\n",
                stats, (unsigned long long)hits, (unsigned long long)misses, (unsigned long)bytes);
        extern uint64_t h2_menu_yield_us, h2_menu_yields;
        xv_logf("[h2/menu-render] profile draws=%llu acquire=%llums transform=%llums raster=%llums yielded=%llums/%llu now=%llums\n",
                (unsigned long long)prof_draws, (unsigned long long)(prof_acquire_us / 1000),
                (unsigned long long)(prof_transform_us / 1000), (unsigned long long)(prof_raster_us / 1000),
                (unsigned long long)(h2_menu_yield_us / 1000), (unsigned long long)h2_menu_yields,
                (unsigned long long)(h2_graphics_time_us() / 1000));
    }
    /* Re-dump every 20 non-empty draws (overwrites): the last dump before a stall
     * holds the fully composited menu frame for offline inspection. */
    if (drawn >= 20 && drawn % 20 == 0) dump_backbuffer(target, W, H, drawn, c->color_offset);
    return 1;
}
