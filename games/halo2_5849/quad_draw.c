#include "quad_draw.h"
#include <string.h>

#define WIDTH 640u
#define HEIGHT 480u
#define BYTES (WIDTH * HEIGHT * 4u)

static int overlap(uintptr_t a, uint32_t an, uintptr_t b, uint32_t bn)
{ return a < b + bn && b < a + an; }

static int pipeline(const h2_quad_draw *q, const h2_command_state *s,
                     const h2_kelvin_clear *c)
{
    const h2_quad_contract *r = q->contract;
    if (!r || !q->render || s->program_start || s->execution_mode != 6 || s->context_write ||
        s->software_valid != 7 || s->dxt1_noise || s->zcull_debug5 || s->rop_control ||
        s->provoking_vertex != 1 || s->edge_flag != 1 || s->compress_depth != 1 ||
        s->shader_inputs || s->shadow_slope != 0x7F800000 ||
        c->format != 0x128 || c->clip_horizontal != (WIDTH << 16) ||
        c->clip_vertical != (HEIGHT << 16) || c->pitch != 0x0A000A00 ||
        memcmp(s->program, r->program, sizeof r->program) ||
        memcmp(s->constants + 10, r->constants, sizeof r->constants) ||
        memcmp(s->setup_valid, r->setup_valid, sizeof r->setup_valid)) return 0;
    /* Only unit 0 is sampled, as linear X8R8G8B8. Palette descriptors do not
     * participate in this non-indexed format or in disabled units 1..3. Keep
     * them stored unchanged, including addresses with no backing allocation;
     * never map or read them. Indexed textures remain unsupported. */
    if ((s->setup[0x1B04 / 4] & ~3u) != 0x00011E28u) return 0;
    for (unsigned unit = 1; unit < 4; ++unit)
        if (s->setup[(0x1B0C + unit * 64) / 4] & 0x40000000u) return 0;
    /* Texture storage may change each decoded frame. All remaining state
     * matches the independently reviewed native73 pipeline exactly. */
    for (unsigned i = 0; i < 2048; ++i) {
        unsigned method = i * 4;
        if (method >= 0x1B20 && method <= 0x1BE0 && (method & 63) == 32) {
            if (s->setup[i] & 0x32u) return 0; /* reserved palette bits */
        } else if (method != 0x1B00 && s->setup[i] != r->setup[i]) return 0;
    }
    return 1;
}

static int resources(const h2_command_state *s, const h2_kelvin_clear *c,
                       h2_linear_texture *texture, uint8_t **destination)
{
    h2_dma_object dma;
    uint32_t physical;
    if (!c->has_color_dma || (c->color_offset & 3) || !c->read_instance || !c->map_physical ||
        !h2_linear_texture_read(s, c, 0, texture) ||
        texture->width != WIDTH || texture->height != HEIGHT || texture->pitch != WIDTH * 4 ||
        (texture->physical & 3) ||
        !h2_dma_load(c->read_instance, c->opaque, c->dma_color, &dma) ||
        !h2_dma_resolve(&dma, c->color_offset, BYTES, 1, c->physical_bytes, &physical) ||
        overlap(physical, BYTES, texture->physical, texture->bytes)) return 0;
    if (c->check_attachment && !c->check_attachment(c->opaque, physical, BYTES,
                                                    WIDTH * 4, 0, c->format)) return 0;
    uint8_t *pixels = c->map_physical(c->opaque, physical, BYTES);
    if (!pixels || (uintptr_t)pixels > UINTPTR_MAX - BYTES ||
        (uintptr_t)texture->pixels > UINTPTR_MAX - texture->bytes ||
        overlap((uintptr_t)pixels, BYTES, (uintptr_t)texture->pixels, texture->bytes)) return 0;
    *destination = pixels;
    return 1;
}

static int complete(h2_quad_draw *q, h2_command_state *s, h2_kelvin_clear *c)
{
    h2_quad_request request;
    uint8_t *destination;
    if (q->vertex != 4 || q->phase || !pipeline(q, s, c) ||
        !resources(s, c, &request.texture, &destination)) return 0;
    memcpy(request.vertices, q->vertices, sizeof request.vertices);
    request.constants = (const uint32_t (*)[4])(s->constants + 10);
    const uint32_t *rgb = q->render(q->opaque, &request);
    if (!rgb || ((uintptr_t)rgb & 3) || (uintptr_t)rgb > UINTPTR_MAX - BYTES ||
        overlap((uintptr_t)rgb, BYTES, (uintptr_t)destination, BYTES) ||
        overlap((uintptr_t)rgb, BYTES, (uintptr_t)request.texture.pixels, request.texture.bytes)) return 0;
    /* The pinned opaque pipeline is ONE/ONE_MINUS_SRC_ALPHA with source alpha
     * one, RGB write mask only, no depth/stencil writes. Commit only these RGB
     * bytes; preserve destination alpha exactly, including zero and non-255. */
    for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) {
        uint32_t color = rgb[i];
        destination[i * 4] = color;
        destination[i * 4 + 1] = color >> 8;
        destination[i * 4 + 2] = color >> 16;
    }
    q->active = 0;
    ++q->completed;
    return 1;
}

int h2_quad_method(h2_quad_draw *q, h2_command_state *s, h2_kelvin_clear *c,
                    uint8_t sub, uint16_t method, uint32_t value)
{
    if (!q || !s || !c || sub >= 8 || s->bound[sub] != 1) return 0;
    if (!q->active) {
        h2_linear_texture texture;
        uint8_t *destination;
        if (method != 0x17FC || value != 7 || !pipeline(q, s, c) ||
            !resources(s, c, &texture, &destination)) return 0;
        memset(q->vertices, 0, sizeof q->vertices);
        q->vertex = q->phase = 0;
        q->active = 1; q->subchannel = sub;
        return 1;
    }
    if (sub != q->subchannel) return 0;
    if (method == 0x17FC) return value == 0 && complete(q, s, c);
    if (q->vertex >= 4) return 0;
    /* Restrict to the observed immediate command ordering. The last component
     * of v0's DATA2F emits one vertex. No arbitrary vertex count or topology. */
    static const uint16_t methods[] = {0x1964, 0x1898, 0x189C, 0x1880, 0x1884};
    static const uint32_t coordinates[4][2] = {
        {0, 0}, {0x44200000, 0}, {0x44200000, 0x43F00000}, {0, 0x43F00000}
    };
    if (q->phase >= 5 || method != methods[q->phase]) return 0;
    h2_quad_vertex *v = &q->vertices[q->vertex];
    if (!q->phase) {
        if (value != UINT32_MAX) return 0;
        for (unsigned i = 0; i < 4; ++i) v->diffuse[i] = 1.0f;
        s->vertex4ub[9] = value;
    } else {
        unsigned component = (q->phase - 1) % 2;
        if (value != coordinates[q->vertex][component]) return 0;
        float *attribute = q->phase <= 2 ? v->texcoord : v->position;
        memcpy(attribute + component, &value, 4);
        attribute[2] = 0.0f; attribute[3] = 1.0f;
    }
    if (++q->phase == 5) { q->phase = 0; ++q->vertex; }
    return 1;
}
