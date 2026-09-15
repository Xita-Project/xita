#include "menu_render.h"
#include "nv2a_vsh.h"
#include "menu_raster.h"
#include "linear_texture.h"
#include "command_state.h"
#include "kelvin_clear.h"
#include <string.h>

extern void xv_logf(const char *, ...);

#define MAX_VERTS 20000u

typedef struct { const uint8_t *base; uint32_t stride, type, count; int enabled; } vattr;

static float rd_f32(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
static int rd_s16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static uint32_t rd_u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static uint32_t attr_bytes(uint32_t type, uint32_t count)
{
    switch (type) {
    case 2: return count * 4;          /* float */
    case 1: case 5: return count * 2;  /* signed short */
    default: return 4;                 /* UB / packed */
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
    case 0: out[0] = p[2] / 255.0f; out[1] = p[1] / 255.0f; out[2] = p[0] / 255.0f; out[3] = p[3] / 255.0f; break; /* BGRA */
    case 4: for (unsigned k = 0; k < 4; ++k) out[k] = p[k] / 255.0f; break; /* RGBA */
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
    v->uv[0] = o[NV2A_O_T0][0]; v->uv[1] = o[NV2A_O_T0][1];
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

int h2_menu_software_render(void *opaque, const h2_menu_request *r)
{
    (void)opaque;
    const h2_command_state *s = r->state;
    const h2_kelvin_clear *c = r->clear;
    if (!s || !c || !c->map_physical || !c->read_instance || !c->has_color_dma) return 0;
    if (!s->program_load) return 0;

    uint32_t W = c->clip_horizontal >> 16, H = c->clip_vertical >> 16;
    if (W < 16 || W > 2048 || H < 16 || H > 2048) return 0;
    const uint32_t bytes = W * H * 4;

    h2_dma_object dmac;
    uint32_t cphys;
    if (!h2_dma_load(c->read_instance, c->opaque, c->dma_color, &dmac) ||
        !h2_dma_resolve(&dmac, c->color_offset, bytes, 1, c->physical_bytes, &cphys)) return 0;
    uint8_t *target = c->map_physical(c->opaque, cphys, bytes);
    if (!target) return 0;

    menu_raster_state rs;
    memset(&rs, 0, sizeof rs);
    rs.target.pixels = target; rs.target.width = W; rs.target.height = H; rs.target.pitch = W * 4;
    rs.blend = MENU_BLEND_ALPHA;
    rs.clip_x0 = 0; rs.clip_y0 = 0; rs.clip_x1 = (int32_t)W - 1; rs.clip_y1 = (int32_t)H - 1;

    h2_linear_texture lt;
    int textured = 0;
    if (h2_linear_texture_read(s, c, 0, &lt) && lt.pixels && lt.pitch == lt.width * 4 &&
        lt.width && lt.height) {
        rs.tex0.texels = (const uint32_t *)(const void *)lt.pixels;
        rs.tex0.width = lt.width; rs.tex0.height = lt.height; textured = 1;
    }

    uint32_t n = 0;
    if (r->vertex_count) {                         /* immediate vertices */
        n = r->vertex_count < MAX_VERTS ? r->vertex_count : MAX_VERTS;
        for (uint32_t i = 0; i < n; ++i) {
            float o[NV2A_VSH_OUTPUTS][4];
            nv2a_vsh_run(s->program, s->program_start, s->program_load,
                         r->vertices[i].attribute, (const float (*)[4])s->constants, o);
            transform_out(o, &g_verts[i]);
        }
        assemble(&rs, r->primitive, n, NULL, n, 0);
    } else {                                       /* indexed / array vertices */
        uint32_t maxv = 0, lo = 0, count;
        const uint16_t *indices = NULL;
        if (r->index_count) {
            for (uint32_t i = 0; i < r->index_count; ++i) if (r->indices[i] > maxv) maxv = r->indices[i];
            indices = r->indices; count = r->index_count;
        } else if (r->array_count) {
            maxv = r->array_start + r->array_count - 1; lo = r->array_start; count = r->array_count;
        } else {
            return 0;
        }
        if (maxv + 1 > MAX_VERTS) return 0;
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
        assemble(&rs, r->primitive, n, indices, count, lo);
    }

    static uint64_t drawn;
    if (drawn < 40 || !(drawn % 200))
        xv_logf("[h2/menu-render] draw=%llu prim=%u verts=%u W=%u H=%u textured=%d color=%08X into back buffer\n",
                (unsigned long long)drawn, r->primitive, n, W, H, textured, c->color_offset);
    ++drawn;
    return 1;
}
