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
/* NV097_GET_REPORT: type 1 (ZPASS_PIXEL_CNT) writes {timestamp lo, hi, value, status 0} at
 * DMA_REPORT (context slot 10, method 0x1A8) + offset, as the GPU does when the query
 * completes. Accepted work is synchronous, so the report is complete when written. */
uint64_t h2_menu_zpass_read(void) __attribute__((weak));
void h2_menu_zpass_clear(void) __attribute__((weak));
static int get_report(h2_command_state *s, h2_kelvin_clear *c, uint32_t value)
{
    uint32_t type = value >> 24, offset = value & 0x00FFFFFFu;
    h2_dma_object dma; uint32_t physical;
    if (type != 1 || (offset & 15) || !(s->dma_valid & (1u << 10)) ||
        !h2_dma_load(c->read_instance, c->opaque, s->dma[10], &dma) ||
        !h2_dma_resolve(&dma, offset, 16, 1, c->physical_bytes, &physical) || (physical & 15)) return 0;
    void *pointer = c->map_physical(c->opaque, physical, 16);
    if (!pointer || ((uintptr_t)pointer & 3) || (uintptr_t)pointer > UINTPTR_MAX - 16) return 0;
    uint64_t count = h2_menu_zpass_read ? h2_menu_zpass_read() : 0, stamp = ++s->report_serial;
    uint32_t words[4] = { (uint32_t)stamp, (uint32_t)(stamp >> 32), count > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)count, 0 };
    uint8_t bytes[16];
    for (unsigned i = 0; i < 4; ++i) for (unsigned k = 0; k < 4; ++k) bytes[i * 4 + k] = (uint8_t)(words[i] >> (8 * k));
    memcpy(pointer, bytes, 16);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    ++s->zpass_reports; s->last_report_address = physical; s->last_report_value = words[2];
    return 1;
}
static int simple_graph_context(h2_kelvin_clear *c, uint32_t instance, uint32_t klass)
{
    if (!instance || (instance & 15) || instance > 0xFFFF0) return 0;
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t word;
        if (!c->read_instance(c->opaque, instance + i * 4, &word) || word != (i ? 0 : klass)) return 0;
    }
    return 1;
}
static int copy_display_image(h2_command_state *s, h2_kelvin_clear *c, uint32_t dimensions)
{
    /* Native79 display preservation: original zero-origin 640x480 SRCCOPY,
     * linear A8R8G8B8 with equal contiguous pitches. Other blits still reject. */
    const uint32_t bytes = 640 * 480 * 4;
    if (dimensions != 0x01E00280 || s->surfaces_valid != 15 || s->blit_point_valid != 3 ||
        s->surfaces_format != 0xA || s->surfaces_pitch != 0x0A000A00 ||
        s->blit_point[0] || s->blit_point[1] || s->blit_operation != 3 ||
        !s->objects[2].present || !s->objects[3].present ||
        s->blit_context[6] != s->objects[3].instance ||
        !simple_graph_context(c, s->objects[2].instance, 0x9F) ||
        !simple_graph_context(c, s->objects[3].instance, 0x62) ||
        s->completed_blits == UINT64_MAX || s->copied_bytes > UINT64_MAX - bytes) return 0;
    for (unsigned i = 0; i < 6; ++i)
        if (!simple_graph_context(c, s->blit_context[i], 0x30)) return 0;
    uint32_t physical[2]; void *mapped[2];
    for (unsigned i = 0; i < 2; ++i) {
        h2_dma_object dma;
        if (!h2_dma_load(c->read_instance, c->opaque, s->surfaces_dma[i], &dma) ||
            !h2_dma_resolve(&dma, s->surfaces_offset[i], bytes, i, c->physical_bytes, &physical[i]) ||
            (physical[i] & 3) ||
            (c->check_attachment && !c->check_attachment(c->opaque, physical[i], bytes, 2560, 0, 0x128))) return 0;
        mapped[i] = c->map_physical(c->opaque, physical[i], bytes);
        if (!mapped[i] || (uintptr_t)mapped[i] > UINTPTR_MAX - bytes) return 0;
    }
    /* Reject both physical overlap and non-injective host aliases before the
     * first write. Source pixels already include completed synchronous GXM RGB
     * commits; preserve all four original bytes including alpha. */
    if (((uint64_t)physical[0] < (uint64_t)physical[1] + bytes &&
         (uint64_t)physical[1] < (uint64_t)physical[0] + bytes) ||
        ((uintptr_t)mapped[0] < (uintptr_t)mapped[1] + bytes &&
         (uintptr_t)mapped[1] < (uintptr_t)mapped[0] + bytes)) return 0;
    memcpy(mapped[1], mapped[0], bytes);
    ++s->completed_blits; s->copied_bytes += bytes;
    s->last_blit_source = physical[0]; s->last_blit_dest = physical[1];
    return 1;
}
static int setup_method(h2_command_state *s, uint16_t method, uint32_t value)
{
    /* State assignments from the observed constructor. Unknown ranges and all
     * execution methods remain excluded, even if their opcode is adjacent. */
    if ((method >= 0xA60 && method <= 0xA9C) || /* combiner factors */
        (method >= 0xAE0 && method <= 0xAEC)) { /* texture color keys */
        /* Full packed color values. */
    } else if ((method >= 0x260 && method <= 0x27C) || /* alpha inputs */
               (method >= 0xAA0 && method <= 0xABC) || /* alpha outputs */
               (method >= 0xAC0 && method <= 0xADC) || /* color inputs */
               (method >= 0x1E40 && method <= 0x1E5C) || /* color outputs */
               method == 0x288 || method == 0x28C || /* final combiner */
               method == 0x1E20 || method == 0x1E24 || /* final factors */
               method == 0x1E60 || method == 0x17F8) { /* control/clip mode */
        /* Native69: exact register assignments, not shader execution. Each
         * eight-stage bank and the final words retain all input bits. A future
         * draw backend must validate the selected combiner modes and sources;
         * BEGIN_END and every emission path still reject without mutation. */
    } else if (method >= 0x1500 && method <= 0x152C) {
        /* Viewport/clip float bounds the menu sets per batch: observed corners
         * (0,0)/(640,480) and max depth 16777215. Stored as exact float bits;
         * a draw backend applies them as the scissor/clip. No execution here. */
    } else if (method >= 0x1480 && method <= 0x14FC) {
        /* Native182: 32 original all-ones polygon-stipple rows. Retain every
         * bit of all rows; no framebuffer, geometry or mask execution here. */
    } else if (method >= 0x1720 && method <= 0x175C) {
        /* Vertex-array offsets: bit 31 selects DMA B, the other bits are the
         * byte offset. Retain the complete input; mapping and full-span checks
         * belong to draw execution, which is not provided by this path. */
    } else if (method >= 0x1760 && method <= 0x179C) {
        /* Native179 uses FLOAT arrays (including disabled size zero) and the
         * packed signed 11/11/10 type. Native195 adds UB_D3D count four:
         * normalized BGRA bytes, still only an exact state assignment here.
         * Bits 8..31 are byte stride; no vertex is fetched or converted here.
         * Active immediate draws are routed to their consumer before setup. */
        unsigned type = value & 15, count = (value >> 4) & 15;
        /* Admitted NV2A array types. The menu uses signed-short (type 1) and
         * float/packed alongside the movie's set; a draw backend still fetches
         * and validates each selected span. UB (0/4) is D3DCOLOR/OGL bytes. */
        if (!((type == 2 && count <= 4) ||               /* float */
              (type == 6 && count == 1) ||               /* packed 11/11/10 */
              (type == 0 && count >= 1 && count <= 4) ||  /* UB D3DCOLOR (menu screen: 1-byte attrs) */
              (type == 4 && count >= 1 && count <= 4) ||  /* UB OGL */
              (type == 1 && count >= 1 && count <= 4) ||  /* signed short normalized */
              (type == 5 && count >= 1 && count <= 4)))   /* signed short */
            return 0;
    } else if (method >= 0x1880 && method <= 0x1AEC) {
        /* Immediate "current vertex" attributes (SET_VERTEX_DATA 2F/2S/4UB/4S/4F,
         * attributes 0..14) written outside BEGIN/END. On hardware these set the
         * persistent current-vertex value; no vertex is emitted here. Store the
         * exact bits; the draw backend seeds per-vertex inputs the arrays do not
         * supply from these. Emission inside a draw is owned by the geometry
         * consumer, which intercepts these before this path. (kelvin() routes the
         * 4UB slots 0x194C-0x1960 to vertex4ub before reaching here.) */
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
        case 0x00: /* offset */
        case 0x04: /* format, including original DMA selector encoding */
        case 0x08: /* addressing */
        case 0x10: /* pitch/control */
        case 0x14: /* filtering */
        case 0x1C: /* rectangle */
            /* Original method inputs only, even for unmapped/invalid resource
             * descriptions. No DMA mapping, texture read or upload occurs.
             * Draw execution must decode and validate every selected input
             * and the complete source span before accessing guest memory. */
            break;
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
        default: return 0; /* the 18h hole is not a texture-state method */
        }
    } else switch (method) {
    case 0x300: case 0x304: case 0x308: case 0x30C: case 0x310:
    case 0x320: case 0x324: case 0x32C: case 0x330: case 0x334: case 0x338:
    case 0x35C: case 0x3A4: case 0x147C: case 0x17BC: case 0x17C4:
    case 0x2A4: case 0x314: case 0x318: case 0x31C: case 0x3B8:
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
    case 0x1E74: if (value & ~0xFFFu) return 0;
        break; /* low shader-control field; other-stage inputs stay separate */
    case 0x294: if (value & ~0x30001u) return 0;
        break; /* separate specular, local eye, alpha from material */
    case 0x29C:
        if (value != 0x2601 && !(value >= 0x800 && value <= 0x804)) return 0;
        break; /* fog equation */
    case 0x2A0: if (value > 3 && value != 6) return 0;
        break; /* fog source */
    case 0x3BC: if (value & ~0xFFFFu) return 0;
        break; /* eight two-bit light modes */
    case 0x43C: if (value > 0x1FF) return 0;
        break; /* fixed-point point size */
    case 0x9C0: case 0x9C4: case 0x9C8: /* original fog parameter bits */
    case 0x1E70: /* texture shader-stage modes, validated by a future draw */
        break;
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
    case 0x1710: /* INVALIDATE_VERTEX_CACHE_FILE: Halo 2 pushes it (value 0) from 0x439D0 after
                  * updating a vertex-buffer entry. No vertex cache exists on this path - every
                  * draw fetches its arrays from guest memory - so the write is a no-op. */
        if (value) return 0;
        return 1;
    case 0x16BC: if (value > 1) return 0; s->edge_flag = value; return 1;
    case 0x17C8: /* CLEAR_REPORT_VALUE: only the Z-pass pixel counter exists */
        if (value != 1) return 0;
        if (h2_menu_zpass_clear) h2_menu_zpass_clear();
        return 1;
    case 0x17CC: if (value > 1) return 0; s->zpass_enable = value; return 1;
    case 0x17D0: return get_report(s, c, value);
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
            if (value != 3) return 0; /* only SRCCOPY */
            s->blit_operation = value; return 1;
        }
        if (method >= 0x184 && method <= 0x19C) {
            h2_command_object o;
            if (!graph_object(c, value, &o) ||
                (o.context[0] & 0xFFF) != (method == 0x19C ? 0x62 : 0x30)) return 0;
            s->blit_context[(method - 0x184) / 4] = o.instance; return 1;
        }
        if (method == 0x300 || method == 0x304) {
            if (value) return 0; /* observed zero-origin copy only */
            unsigned index = (method - 0x300) / 4;
            s->blit_point[index] = value; s->blit_point_valid |= 1u << index; return 1;
        }
        if (method == 0x308) return copy_display_image(s, c, value);
        return 0;
    case 4:
        if (method >= 0x300 && method <= 0x30C) {
            unsigned index = (method - 0x300) / 4;
            if (method == 0x300) {
                if (value != 0xA) return 0;
                s->surfaces_format = value;
            } else if (method == 0x304) {
                if (value != 0x0A000A00) return 0;
                s->surfaces_pitch = value;
            } else {
                if (value & ~0x07FFFFFCu) return 0;
                s->surfaces_offset[index - 2] = value;
            }
            s->surfaces_valid |= 1u << index; return 1;
        }
        if ((method != 0x184 && method != 0x188) || !dma_object(c, value, &instance)) return 0;
        s->surfaces_dma[(method - 0x184) / 4] = instance; return 1;
    case 5:
        if (method != 0x310) return 0;
        s->pattern_color = value; return 1;
    default: return 0;
    }
}
