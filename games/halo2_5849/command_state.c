#include "command_state.h"
#include <string.h>

static int graph_object(h2_kelvin_clear *c, uint32_t handle, h2_command_object *out)
{
    h2_gpu_object o;
    if (!h2_object_lookup(c->read_instance, c->opaque, handle, c->channel, &o) || o.engine != 1) return 0;
    memset(out, 0, sizeof *out);
    for (unsigned i = 0; i < 4; ++i)
        if (!c->read_instance(c->opaque, o.instance + i * 4, &out->context[i])) return 0;
    out->instance = o.instance; out->present = 1;
    return 1;
}
static int dma_object(h2_kelvin_clear *c, uint32_t handle, uint32_t *instance)
{
    h2_gpu_object o; h2_dma_object dma;
    if (!h2_object_lookup(c->read_instance, c->opaque, handle, c->channel, &o) || o.engine != 0 ||
        !h2_dma_load(c->read_instance, c->opaque, o.instance, &dma)) return 0;
    *instance = o.instance;
    return 1;
}
static int bind_object(h2_command_state *s, h2_kelvin_clear *c, unsigned sub,
                        uint32_t value, uint32_t source)
{
    h2_command_object o;
    if (!graph_object(c, value, &o)) return 0;
    unsigned index;
    switch (o.context[0] & 0xFFF) {
    case 0x97: index = 0; break;
    case 0x39: index = 1; break;
    case 0x9F: index = 2; break;
    case 0x62: index = 3; break;
    case 0x44: index = 4; break;
    default: return 0;
    }
    /* One persistent object per supported class. No guessed context switching. */
    if (s->objects[index].present && s->objects[index].instance != o.instance) return 0;
    if (index == 0 && !h2_kelvin_clear_method(c, sub, 0, value, source)) return 0;
    s->objects[index] = o; s->bound[sub] = index + 1;
    return 1;
}
static int release_semaphore(h2_command_state *s, h2_kelvin_clear *c, uint32_t value)
{
    h2_dma_object dma; uint32_t physical;
    if (!(s->dma_valid & (1u << 9)) || (s->semaphore_offset & 3) ||
        !h2_dma_load(c->read_instance, c->opaque, s->dma[9], &dma) ||
        !h2_dma_resolve(&dma, s->semaphore_offset, 4, 1, c->physical_bytes, &physical) ||
        (physical & 3)) return 0;
    void *pointer = c->map_physical(c->opaque, physical, 4);
    if (!pointer || ((uintptr_t)pointer & 3) || (uintptr_t)pointer > UINTPTR_MAX - 4) return 0;
    uint8_t bytes[4]; uint32_t native;
    for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
    memcpy(&native, bytes, 4);
    /* Accepted work is synchronous. Publish its real guest-RAM completion word
     * after prior host pixel writes; there is no fabricated hardware IRQ. */
    __atomic_store_n((uint32_t *)pointer, native, __ATOMIC_RELEASE);
    ++s->semaphore_releases;
    s->last_semaphore_address = physical; s->last_semaphore_value = value;
    return 1;
}
static int kelvin(h2_command_state *s, h2_kelvin_clear *c, unsigned sub,
                   uint16_t method, uint32_t value, uint32_t source)
{
    if (method >= 0x180 && method <= 0x1A8 && method != 0x18C) {
        unsigned slot = (method - 0x180) / 4;
        uint32_t instance;
        if (!dma_object(c, value, &instance)) return 0;
        if ((method == 0x194 || method == 0x198) &&
            !h2_kelvin_clear_method(c, sub, method, value, source)) return 0;
        s->dma[slot] = instance; s->dma_valid |= 1u << slot;
        return 1;
    }
    /* Fixed-function planes and program constants share the Cheops context. */
    if (method >= 0x840 && method <= 0x93C) {
        unsigned slot = (method - 0x840) / 4;
        s->constants[0x40 + (slot / 16) * 8 + (slot % 16) / 4][slot % 4] = value;
        return 1;
    }
    if (method >= 0x9D0 && method <= 0x9DC) {
        s->constants[0x39][(method - 0x9D0) / 4] = value; return 1;
    }
    if (method >= 0xA50 && method <= 0xA5C) {
        s->constants[0x38][(method - 0xA50) / 4] = value; return 1;
    }
    if (method >= 0xB80 && method <= 0xBFC) {
        if (s->constant_load >= 192) return 0;
        unsigned component = ((method - 0xB80) / 4) % 4;
        s->constants[s->constant_load][component] = value;
        if (component == 3) ++s->constant_load;
        return 1;
    }
    switch (method) {
    case 0x120: if (value > 7) return 0; s->flip_read = value; return 1;
    case 0x124: if (value > 7) return 0; s->flip_write = value; return 1;
    case 0x128: if (value > 7) return 0; s->flip_modulo = value; return 1;
    case 0x9FC: if (value > 1) return 0; s->provoking_vertex = value; return 1;
    case 0x16BC: if (value > 1) return 0; s->edge_flag = value; return 1;
    case 0x1D6C: if (value & 3) return 0; s->semaphore_offset = value; return 1;
    case 0x1D70: return release_semaphore(s, c, value);
    /* Compression is metadata in this canonical logical-depth backend. */
    case 0x1D80: if (value > 1) return 0; s->compress_depth = value; return 1;
    case 0x1E68: s->shadow_slope = value; return 1;
    case 0x1E78: if (value & ~0x0FFFF000u) return 0; s->shader_inputs = value; return 1;
    case 0x1EA4: if (value >= 192) return 0; s->constant_load = value; return 1;
    default: return h2_kelvin_clear_method(c, sub, method, value, source);
    }
}
int h2_command_method(h2_command_state *s, h2_kelvin_clear *c, uint8_t sub,
                       uint16_t method, uint32_t value, uint32_t source)
{
    if (!s || !c || !c->read_instance || !c->map_physical || sub > 7 || (method & 3)) return 0;
    if (!method) return bind_object(s, c, sub, value, source);
    uint32_t instance;
    switch (s->bound[sub]) {
    case 1: return kelvin(s, c, sub, method, value, source);
    case 2: /* M2MF notifier binding only; copy/notify execution rejects. */
        if (method != 0x180 || !dma_object(c, value, &instance)) return 0;
        s->m2mf_notifier = instance; return 1;
    case 3:
        if (method == 0x2FC) {
            if (value != 3) return 0; /* SRCCOPY state; blit execution rejects. */
            s->blit_operation = value; return 1;
        }
        if (method >= 0x184 && method <= 0x19C) {
            h2_command_object o;
            if (!graph_object(c, value, &o) ||
                (o.context[0] & 0xFFF) != (method == 0x19C ? 0x62 : 0x30)) return 0;
            s->blit_context[(method - 0x184) / 4] = o.instance; return 1;
        }
        return 0;
    case 4:
        if ((method != 0x184 && method != 0x188) || !dma_object(c, value, &instance)) return 0;
        s->surfaces_dma[(method - 0x184) / 4] = instance; return 1;
    case 5:
        if (method != 0x310) return 0;
        s->pattern_color = value; return 1;
    default: return 0;
    }
}
