#include "blur_draw.h"
#include <string.h>
#define BYTES (160u * 120u * 4u)
#define INPUT_BYTES (160u * 120u * 4u)

static int overlaps(uintptr_t a, uint32_t an, uintptr_t b, uint32_t bn)
{ return a < b + bn && b < a + an; }

static int pipeline(const h2_blur_draw *q, const h2_command_state *s,
                      const h2_kelvin_clear *c)
{
    const h2_blur_contract *r = q->contract;
    if (!r || !q->render || s->program_start || s->execution_mode != 6 || s->context_write ||
        s->software_valid != 7 || s->dxt1_noise || s->zcull_debug5 || s->rop_control ||
        s->provoking_vertex != 1 || s->edge_flag != 1 || s->compress_depth != 1 ||
        s->shader_inputs || s->shadow_slope != 0x7F800000 ||
        c->format != 0x128 || c->clip_horizontal != (160u << 16) ||
        c->clip_vertical != (120u << 16) || c->pitch != 0x0A000280 ||
        memcmp(s->program, r->program, sizeof r->program) ||
        memcmp(s->setup_valid, r->setup_valid, sizeof r->setup_valid)) return 0;
    /* The trusted pinned reference must also remain inside the backend's
     * explicit framebuffer/sampler operations; changing a reference file
     * cannot grant depth writes, alpha tests, other blending or formats. */
    static const uint32_t required[][2] = {
        {0x300,0},{0x304,0},{0x308,0},{0x30C,0},{0x314,0},{0x32C,0},
        {0x358,0x01010101},{0x35C,0},{0x398,0x4B7FFFFF},{0x147C,0},
        {0x1E70,0x8421},{0x1E74,0}
    };
    for (unsigned i = 0; i < sizeof required / sizeof *required; ++i)
        if (s->setup[required[i][0] / 4] != required[i][1]) return 0;
    for (unsigned u = 0; u < 4; ++u) {
        unsigned base = (0x1B00 + u * 64) / 4;
        if (s->setup[base + 1] != 0x11229 || s->setup[base + 2] != 0x30303 ||
            s->setup[base + 3] != 0x4003FFC0 || s->setup[base + 4] != 0x02800000 ||
            s->setup[base + 5] != 0x02022000 || s->setup[base + 7] != 0x00A00078) return 0;
    }
    for (unsigned i = 0; i < 2048; ++i) {
        unsigned method = i * 4;
        if (method >= 0x1B20 && method <= 0x1BE0 && (method & 63) == 32) {
            if (s->setup[i] & 0x32) return 0; /* non-indexed palette: no fetch */
        } else if (method != 0x1B00 && method != 0x1B40 && method != 0x1B80 && method != 0x1BC0 && s->setup[i] != r->setup[i]) return 0;
    }
    return 1;
}

static int resources(const h2_command_state *s, const h2_kelvin_clear *c,
                       h2_blur_request *request, uint8_t **destination)
{
    if (!c->has_color_dma || !c->read_instance || !c->map_physical) return 0;
    for (unsigned u = 0; u < 4; ++u)
        if (!h2_linear_texture_read(s, c, u, &request->textures[u]) ||
            (request->textures[u].physical & 3) ||
            request->textures[u].width != 160 || request->textures[u].height != 120 || request->textures[u].pitch != 640 ||
            request->textures[u].bytes != INPUT_BYTES) return 0;
    h2_dma_object dma;
    uint32_t color;
    if (!h2_dma_load(c->read_instance, c->opaque, c->dma_color, &dma) ||
        !h2_dma_resolve(&dma, c->color_offset, BYTES, 1, c->physical_bytes, &color) ||
        (color & 3)) return 0;
    if (c->check_attachment && !c->check_attachment(c->opaque, color, BYTES, 640, 0, c->format)) return 0;
    uint8_t *target = c->map_physical(c->opaque, color, BYTES);
    if (!target || (uintptr_t)target > UINTPTR_MAX - BYTES) return 0;
    for (unsigned u = 0; u < 4; ++u) {
        const h2_linear_texture *t = &request->textures[u];
        if ((uintptr_t)t->pixels > UINTPTR_MAX - INPUT_BYTES ||
            overlaps(color, BYTES, t->physical, INPUT_BYTES) ||
            overlaps((uintptr_t)target, BYTES, (uintptr_t)t->pixels, INPUT_BYTES)) return 0;
    }
    /* Read-only textures may overlap one another, including the four equal
     * original allocations. None may overlap the written color attachment.
     * No guest zeta resource is mapped: depth and stencil are disabled. */
    request->destination = target;
    *destination = target;
    return 1;
}

static unsigned matching_patterns(const h2_blur_draw *q, unsigned vertex,
                                  unsigned attribute, unsigned component, uint32_t value)
{
    unsigned matches = 0;
    for (unsigned pattern = 0; pattern < 4; ++pattern) {
        uint32_t expected;
        memcpy(&expected, &q->contract->vertices[pattern][vertex].attribute[attribute][component], 4);
        if (value == expected) matches |= 1u << pattern;
    }
    return matches;
}
static int finish(h2_blur_draw *q, h2_command_state *s, h2_kelvin_clear *c)
{
    h2_blur_request request;
    uint8_t *destination;
    if (q->vertex != 4 || q->phase || !pipeline(q, s, c) ||
        !resources(s, c, &request, &destination)) return 0;
    unsigned candidates = q->candidates & 15;
    for (unsigned v=0;v<4;++v) for (unsigned a=0;a<7;++a) for (unsigned k=0;k<4;++k) {
        uint32_t value;memcpy(&value,&q->vertices[v].attribute[a][k],4);
        candidates &= matching_patterns(q,v,a,k,value);
        if (!candidates) return 0;
    }
    memcpy(request.vertices, q->vertices, sizeof request.vertices);
    for (unsigned i = 0; i < 18; ++i) {
        unsigned method = i < 16 ? 0xA60 + i * 4 : 0x1E20 + (i - 16) * 4;
        uint32_t color = s->setup[method / 4];
        static const unsigned shifts[] = {16,8,0,24};
        for (unsigned k = 0; k < 4; ++k) request.factors[i][k] = ((color >> shifts[k]) & 255) / 255.0f;
    }
    const uint32_t *rgba = q->render(q->opaque, &request);
    if (!rgba || ((uintptr_t)rgba & 3) || (uintptr_t)rgba > UINTPTR_MAX - BYTES ||
        overlaps((uintptr_t)rgba, BYTES, (uintptr_t)destination, BYTES)) return 0;
    for (unsigned u = 0; u < 4; ++u)
        if (overlaps((uintptr_t)rgba, BYTES, (uintptr_t)request.textures[u].pixels, INPUT_BYTES)) return 0;
    memcpy(destination, rgba, BYTES);
    q->active = 0; ++q->completed;
    return 1;
}

int h2_blur_method(h2_blur_draw *q, h2_command_state *s, h2_kelvin_clear *c,
                      uint8_t sub, uint16_t method, uint32_t value)
{
    if (!q || !s || !c || !q->contract || sub >= 8 || s->bound[sub] != 1) return 0;
    if (!q->active) {
        h2_blur_request request;
        uint8_t *destination;
        if (method != 0x17FC || value != 7 || !pipeline(q, s, c) ||
            !resources(s, c, &request, &destination)) return 0;
        memset(q->vertices, 0, sizeof q->vertices);
        q->vertex = q->phase = 0; q->active = 1; q->subchannel = sub; q->candidates = 15;
        return 1;
    }
    if (sub != q->subchannel) return 0;
    if (method == 0x17FC) return value == 0 && finish(q, s, c);
    if (q->vertex >= 4 || q->phase >= 24) return 0;
    static const unsigned methods[] = {0x1A50,0x1A40,0x1A30,0x1A20,0x1A10,0x1518};
    unsigned field = q->phase / 4, component = q->phase % 4, attribute = 5 - field;
    if (method != methods[field] + component * 4) return 0;
    /* Narrow to complete observed patterns; mixed-pattern primitives reject.
     * Consume the actual 24-word packets and preserve their bits.
     * The pinned contract authorizes inputs but never supplies commands. */
    unsigned candidates = q->candidates & matching_patterns(q,q->vertex,attribute,component,value);
    if (!candidates) return 0;
    q->candidates = candidates;
    memcpy(&q->vertices[q->vertex].attribute[attribute][component], &value, 4);
    if (++q->phase == 24) { q->phase = 0; ++q->vertex; }
    return 1;
}
