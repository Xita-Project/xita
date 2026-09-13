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
    case 0x97: index = 0;
        break;
    case 0x39: index = 1;
        break;
    case 0x9F: index = 2;
        break;
    case 0x62: index = 3;
        break;
    case 0x44: index = 4;
        break;
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
static int setup_method(h2_command_state *s, uint16_t method, uint32_t value)
{
    /* State assignments from the observed constructor. Unknown ranges and all
     * execution methods remain excluded, even if their opcode is adjacent. */
    if ((method >= 0xA60 && method <= 0xA9C) || /* combiner factors */
        (method >= 0xAE0 && method <= 0xAEC)) { /* texture color keys */
        /* Full packed color values. */
    } else if (method >= 0x1AF0 && method <= 0x1AFC) {
        /* SET_VERTEX_DATA4F attribute 15, one exact component per method.
         * Attribute 0's final component emits a vertex and stays unsupported.
         * BEGIN_END and every vertex-emission path reject, so there can be no
         * pending vertices whose old attributes need materializing here.
         * Preserve the raw float bits and per-component validity for a future
         * backend; this neither executes a shader nor emits geometry. */
    } else if (method >= 0x3C0 && method <= 0x3FC) { /* four texgen S/T/R/Q slots */
        if (value != 0 && value != 0x2400 && value != 0x2401 && value != 0x2402 &&
            value != 0x8511 && value != 0x8512) return 0;
    } else if (method >= 0x1B00 && method <= 0x1BFC) {
        switch ((method - 0x1B00) & 63) {
        case 0x0C: /* texture control: enable/lod state only */
        case 0x24: /* border color */
        case 0x28: case 0x2C: case 0x30: case 0x34: /* bump matrix */
        case 0x38: case 0x3C: /* bump scale/offset */
            break;
        case 0x20: /* palette descriptor: DMA bit 0, length bits 2..3, offset 6..31 */
            if (value & 0x32u) return 0;
            /* State assignment only: no palette read, allocation or decoding.
             * A future draw must resolve the selected DMA and validate the
             * entire palette span before touching guest data. */
            break;
        default: return 0; /* texture resources/format/filter need separate validation */
        }
    } else switch (method) {
    case 0x300: case 0x304: case 0x308: case 0x30C: case 0x310:
    case 0x320: case 0x324: case 0x32C: case 0x330: case 0x334: case 0x338:
    case 0x35C: case 0x3A4: case 0x147C: case 0x17BC: case 0x17C4:
        if (value > 1) return 0;
        break; /* enable bits */
    case 0x328: if (value > 6) return 0;
        break; /* skin mode */
    case 0x33C: case 0x354: case 0x364: /* alpha/depth/stencil compare functions */
        if (value < 0x200 || value > 0x207) return 0;
        break;
    case 0x340: case 0x368: if (value > 255) return 0;
        break; /* alpha/stencil reference */
    case 0x344: case 0x348: /* blend factors */
        if (value > 1 && !(value >= 0x300 && value <= 0x308) &&
            !(value >= 0x8001 && value <= 0x8004)) return 0;
        break;
    case 0x350: /* blend equation */
        if (value != 0x8006 && value != 0x8007 && value != 0x8008 && value != 0x800A &&
            value != 0x800B && value != 0xF005 && value != 0xF006) return 0;
        break;
    case 0x358: if (value & ~0x01010101u) return 0;
        break; /* color write mask */
    case 0x370: case 0x374: case 0x378: /* stencil operations */
        if (value && value != 0x1E00 && value != 0x1E01 && value != 0x1E02 &&
            value != 0x1E03 && value != 0x150A && value != 0x8507 && value != 0x8508) return 0;
        break;
    case 0x37C: if (value != 0x1D00 && value != 0x1D01) return 0;
        break; /* shading */
    case 0x380: if (value > 0x1FF) return 0;
        break; /* fixed-point line width */
    case 0x38C: case 0x390: if (value < 0x1B00 || value > 0x1B02) return 0;
        break;
    case 0x39C: if (value != 0x404 && value != 0x405 && value != 0x408) return 0;
        break;
    case 0x3A0: if (value != 0x900 && value != 0x901) return 0;
        break;
    case 0x2B4: if (value > 1) return 0;
        break; /* window inclusion/exclusion */
    case 0x2C0: case 0x2E0: if (value & 0xF000F000u) return 0;
        break; /* first window */
    case 0x9F8: if (value > 4) return 0;
        break; /* raster swath width */
    case 0x1D84: if (value & ~3u) return 0;
        break; /* hierarchical depth/stencil optimization */
    case 0x1E6C: if (value > 7) return 0;
        break; /* shadow compare */
    case 0x290: /* control: clear format checked at execution */
    case 0x2A8: case 0x34C: /* packed colors */
    case 0x360: case 0x36C: /* stencil mask inputs (low 8 bits used by future draws) */
    case 0x384: case 0x388: case 0x394: case 0x398: /* floating-point depth/offset inputs */
    case 0x1D78: case 0x1D7C: /* depth clamp / AA metadata; clear has its own guard */
        break;
    default: return 0;
    }
    s->setup[method / 4] = value;
    s->setup_valid[(method / 4) / 32] |= 1u << ((method / 4) % 32);
    return 1;
}
static int has_setup(const h2_command_state *s, uint16_t method)
{ return !!(s->setup_valid[(method / 4) / 32] & (1u << ((method / 4) % 32))); }
static int software_method(h2_command_state *s, const h2_kelvin_clear *c, uint32_t value)
{
    /* Original 3FF240 takes selector in bits 0..4 and argument above bit 4.
     * Selector 8 writes the argument to RDI E0:50 and DF:08 (DXT1 noise).
     * Selector 9 writes the color-clear parameter to BAR + depth-clear parameter.
     * Translate only the audited constructor's settings into typed host state;
     * never expose its arbitrary register-write protocol or discard callbacks. */
    if (!value) return 1;
    if (value == 8 || value == 0x28) {
        s->dxt1_noise = value >> 5; s->software_valid |= 1;
    } else if (value == 9 && !c->clear_color) {
        if (c->clear_zstencil == 0x400094) {
            s->zcull_debug5 = 0; s->software_valid |= 2;
        } else if (c->clear_zstencil == 0x400B80) {
            s->rop_control = 0; s->software_valid |= 4;
        } else return 0;
    } else return 0;
    ++s->software_updates;
    return 1;
}
static int clear_setup_supported(const h2_command_state *s, const h2_kelvin_clear *c, uint32_t flags)
{
    if (!flags) return 1;
    if ((s->setup[0x1D7C / 4] & 1) || ((flags & 3) && (s->setup[0x290 / 4] & 0x1000)) ||
        ((flags & 0xF0) && s->setup[0x310 / 4])) return 0;
    if (s->setup[0x2B4 / 4]) return 0;
    if (has_setup(s, 0x2C0) || has_setup(s, 0x2E0)) {
        if (!has_setup(s, 0x2C0) || !has_setup(s, 0x2E0)) return 0;
        uint32_t x = s->setup[0x2C0 / 4], y = s->setup[0x2E0 / 4];
        /* Require the whole clear strictly inside the first inclusive window.
         * This also covers an exclusive upper-bound interpretation. Narrower
         * clipping and other window slots are not implemented. */
        if ((x & 0xFFF) > (c->clear_horizontal & 0xFFF) ||
            (y & 0xFFF) > (c->clear_vertical & 0xFFF) ||
            ((x >> 16) & 0xFFF) <= ((c->clear_horizontal >> 16) & 0xFFF) ||
            ((y >> 16) & 0xFFF) <= ((c->clear_vertical >> 16) & 0xFFF)) return 0;
    }
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
    if (method >= 0xA20 && method <= 0xA2C) {
        s->constants[0x3B][(method - 0xA20) / 4] = value; return 1;
    }
    if (method >= 0xAF0 && method <= 0xAFC) {
        s->constants[0x3A][(method - 0xAF0) / 4] = value; return 1;
    }
    if (method >= 0xB00 && method <= 0xB7C) {
        if (s->program_load >= 136) return 0;
        unsigned component = ((method - 0xB00) / 4) % 4;
        s->program[s->program_load][component] = value;
        if (component == 3) ++s->program_load;
        return 1;
    }
    switch (method) {
    case 0x100: return software_method(s, c, value);
    case 0x110:
        if (value) return 0;
        __atomic_thread_fence(__ATOMIC_SEQ_CST); return 1; /* accepted work is synchronous */
    case 0x194C: case 0x1950: case 0x195C: case 0x1960:
        s->vertex4ub[(method - 0x1940) / 4] = value; return 1;
    case 0x1E94:
        if ((value & ~7u) || (value & 3) == 3) return 0;
        s->execution_mode = value; return 1;
    case 0x1E98: if (value > 1) return 0; s->context_write = value; return 1;
    case 0x1E9C: if (value >= 136) return 0; s->program_load = value; return 1;
    case 0x1EA0: if (value >= 136) return 0; s->program_start = value; return 1;
    case 0x1D94:
        if (!clear_setup_supported(s, c, value)) return 0;
        return h2_kelvin_clear_method(c, sub, method, value, source);
    case 0x120: if (value > 7) return 0; s->flip_read = value; return 1;
    case 0x124: if (value > 7) return 0; s->flip_write = value; return 1;
    case 0x128: if (value > 7) return 0; s->flip_modulo = value; return 1;
    case 0x12C:
        if (value || s->flip_modulo < 2 || s->flip_write >= s->flip_modulo ||
            s->flip_read >= s->flip_modulo) return 0;
        s->flip_write = (s->flip_write + 1) % s->flip_modulo; return 1;
    case 0x130:
        /* A stalled asynchronous flip has no completion source yet. Stop it;
         * only already completed synchronous work may pass this condition. */
        return !value && s->flip_modulo >= 2 && s->flip_write < s->flip_modulo &&
               s->flip_read < s->flip_modulo && s->flip_read != s->flip_write;
    case 0x9FC: if (value > 1) return 0; s->provoking_vertex = value; return 1;
    case 0x16BC: if (value > 1) return 0; s->edge_flag = value; return 1;
    case 0x1D6C: if (value & 3) return 0; s->semaphore_offset = value; return 1;
    case 0x1D70: return release_semaphore(s, c, value);
    /* Compression is metadata in this canonical logical-depth backend. */
    case 0x1D80: if (value > 1) return 0; s->compress_depth = value; return 1;
    case 0x1E68: s->shadow_slope = value; return 1;
    case 0x1E78: if (value & ~0x0FFFF000u) return 0; s->shader_inputs = value; return 1;
    case 0x1EA4: if (value >= 192) return 0; s->constant_load = value; return 1;
    default:
        if (setup_method(s, method, value)) return 1;
        return h2_kelvin_clear_method(c, sub, method, value, source);
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
