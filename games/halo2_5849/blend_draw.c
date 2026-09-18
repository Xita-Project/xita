#include "blend_draw.h"
#include <string.h>
extern void xv_mark_written(const void *host, uint32_t bytes) __attribute__((weak));
#define BYTES (640u * 480u * 4u)
#define IMAGE_BYTES (160u * 120u * 4u)

static int overlaps(uintptr_t a, uint32_t an, uintptr_t b, uint32_t bn)
{ return a < b + bn && b < a + an; }

static int pipeline(const h2_blend_draw *q, const h2_command_state *s,
                      const h2_kelvin_clear *c)
{
    const h2_blend_contract *r = q->contract;
    if (!r || !q->render || s->program_start || s->execution_mode != 6 || s->context_write ||
        s->software_valid != 7 || s->dxt1_noise || s->zcull_debug5 || s->rop_control ||
        s->provoking_vertex != 1 || s->edge_flag != 1 || s->compress_depth != 1 ||
        s->shader_inputs || s->shadow_slope != 0x7F800000 ||
        c->format != 0x128 || c->clip_horizontal != (640u << 16) ||
        c->clip_vertical != (480u << 16) || c->pitch != 0x0A000A00 ||
        memcmp(s->program, r->program, sizeof r->program) ||
        memcmp(s->setup_valid, r->setup_valid, sizeof r->setup_valid)) return 0;
    /* The trusted pinned reference must also remain inside the backend's
     * explicit framebuffer/sampler operations; changing a reference file
     * cannot grant depth writes, alpha tests, other blending or formats. */
    static const uint32_t required[][2] = {
        {0x300,0},{0x304,1},{0x308,0},{0x30C,0},{0x314,0},{0x32C,0},
        {0x344,0x307},{0x348,1},{0x350,0x8006},
        {0x358,0x01010101},{0x35C,0},{0x398,0x4B7FFFFF},{0x147C,0},
        {0x1B04,0x00011229},{0x1B08,0x00030303},{0x1B0C,0x4003FFC0},
        {0x1B10,0x02800000},{0x1B14,0x02022000},{0x1B1C,0x00A00078},
        {0x1E70,1},{0x1E74,0}
    };
    for (unsigned i = 0; i < sizeof required / sizeof *required; ++i)
        if (s->setup[required[i][0] / 4] != required[i][1]) return 0;
    for (unsigned i = 0; i < 2048; ++i) {
        unsigned method = i * 4;
        if (method >= 0x1B20 && method <= 0x1BE0 && (method & 63) == 32) {
            if (s->setup[i] & 0x32) return 0; /* non-indexed palette: no fetch */
        } else if (method != 0x1B00 && s->setup[i] != r->setup[i]) return 0;
    }
    return 1;
}

/* Only stage0 fetches. The other retained texture descriptors are inactive;
 * neither they nor the disabled depth attachment grant a memory access. */
static int resources(const h2_command_state *s, const h2_kelvin_clear *c,
                       h2_blend_request *request, uint8_t **destination)
{
    if (!c->has_color_dma || !c->read_instance || !c->map_physical ||
        !h2_linear_texture_read(s, c, 0, &request->texture0)) return 0;
    if (request->texture0.width != 160 || request->texture0.height != 120 ||
        request->texture0.pitch != 640 || request->texture0.bytes != IMAGE_BYTES ||
        (request->texture0.physical & 3)) return 0;
    h2_dma_object dma;
    uint32_t color;
    if (!h2_dma_load(c->read_instance, c->opaque, c->dma_color, &dma) ||
        !h2_dma_resolve(&dma, c->color_offset, BYTES, 1, c->physical_bytes, &color) ||
        (color & 3)) return 0;
    /* Depth/stencil are independently disabled by the pipeline guard. */
    if (c->check_attachment &&
        !c->check_attachment(c->opaque, color, BYTES, 2560, 0, c->format)) return 0;
    uint8_t *target = c->map_physical(c->opaque, color, BYTES);
    const uint32_t address[] = {color, request->texture0.physical};
    const uint32_t size[] = {BYTES, IMAGE_BYTES};
    const uintptr_t host[] = {(uintptr_t)target, (uintptr_t)request->texture0.pixels};
    if (!target) return 0;
    for (unsigned i = 0; i < 2; ++i) {
        if (host[i] > UINTPTR_MAX - size[i]) return 0;
        for (unsigned j = 0; j < i; ++j)
            if (overlaps(address[i], size[i], address[j], size[j]) ||
                overlaps(host[i], size[i], host[j], size[j])) return 0;
    }
    request->destination = target;
    *destination = target;
    return 1;
}

static int finish(h2_blend_draw *q, h2_command_state *s, h2_kelvin_clear *c)
{
    h2_blend_request request;
    uint8_t *destination;
    if (q->vertex != 4 || q->phase || !pipeline(q, s, c) ||
        !resources(s, c, &request, &destination) ||
        memcmp(q->vertices, q->contract->vertices, sizeof q->vertices)) return 0;
    memcpy(request.vertices, q->vertices, sizeof request.vertices);
    for (unsigned i = 0; i < 18; ++i) {
        unsigned method = i < 16 ? 0xA60 + i * 4 : 0x1E20 + (i - 16) * 4;
        uint32_t color = s->setup[method / 4];
        static const unsigned shifts[] = {16,8,0,24};
        for (unsigned k = 0; k < 4; ++k) request.factors[i][k] = ((color >> shifts[k]) & 255) / 255.0f;
    }
    const uint32_t *rgba = q->render(q->opaque, &request);
    if (!rgba || ((uintptr_t)rgba & 3) || (uintptr_t)rgba > UINTPTR_MAX - BYTES ||
        overlaps((uintptr_t)rgba, BYTES, (uintptr_t)destination, BYTES) ||
        overlaps((uintptr_t)rgba, BYTES, (uintptr_t)request.texture0.pixels, IMAGE_BYTES)) return 0;
    memcpy(destination, rgba, BYTES);
    if (xv_mark_written) xv_mark_written(destination, BYTES);   /* the store happens now, maybe frames after the mapping */
    q->active = 0; ++q->completed;
    return 1;
}

int h2_blend_method(h2_blend_draw *q, h2_command_state *s, h2_kelvin_clear *c,
                      uint8_t sub, uint16_t method, uint32_t value)
{
    if (!q || !s || !c || !q->contract || sub >= 8 || s->bound[sub] != 1) return 0;
    if (!q->active) {
        h2_blend_request request;
        uint8_t *destination;
        if (method != 0x17FC || value != 7 || !pipeline(q, s, c) ||
            !resources(s, c, &request, &destination)) return 0;
        memset(q->vertices, 0, sizeof q->vertices);
        q->vertex = q->phase = 0; q->active = 1; q->subchannel = sub;
        return 1;
    }
    if (sub != q->subchannel) return 0;
    if (method == 0x17FC) return value == 0 && finish(q, s, c);
    if (q->vertex >= 4 || q->phase >= 12) return 0;
    static const unsigned methods[] = {0x1A50,0x1A10,0x1518};
    static const unsigned attributes[] = {5,1,0};
    unsigned field = q->phase / 4, component = q->phase % 4, attribute = attributes[field];
    if (method != methods[field] + component * 4) return 0;
    uint32_t expected;
    memcpy(&expected, &q->contract->vertices[q->vertex].attribute[attribute][component], 4);
    if (value != expected) return 0;
    memcpy(&q->vertices[q->vertex].attribute[attribute][component], &value, 4);
    if (++q->phase == 12) { q->phase = 0; ++q->vertex; }
    return 1;
}
