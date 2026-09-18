#include "kelvin_clear.h"
#include <string.h>

int h2_kelvin_clear_init(h2_kelvin_clear *state, h2_instance_read read_instance,
                         h2_physical_map map_physical, void *opaque,
                         uint32_t physical_bytes, uint8_t channel)
{
    if (!state || !read_instance || !map_physical || !physical_bytes || channel > 31) return 0;
    memset(state, 0, sizeof *state);
    state->read_instance = read_instance; state->map_physical = map_physical;
    state->opaque = opaque; state->physical_bytes = physical_bytes; state->channel = channel;
    return 1;
}

static int lookup_dma(const h2_kelvin_clear *state, uint32_t handle, uint32_t *instance)
{
    h2_gpu_object object; h2_dma_object dma;
    if (!h2_object_lookup(state->read_instance, state->opaque, handle, state->channel, &object) ||
        object.engine != 0 || !h2_dma_load(state->read_instance, state->opaque, object.instance, &dma)) return 0;
    *instance = object.instance;
    return 1;
}

typedef struct target {
    uint8_t *data;
    uint32_t physical, bytes, pitch, mask, value;
} target;

/* Colour targets about to be written by the CPU: a GPU-resident menu backend (menu_gxm.c)
 * lands or discards its pending contents first. Weak: absent in the state-only build and tests. */
extern void h2_menu_gxm_before_cpu_write(uint32_t physical, uint32_t bytes, int whole_surface) __attribute__((weak));
static int g_whole_surface_clear;   /* set by clear_surface before mapping its targets */
static int map_target(const h2_kelvin_clear *state, uint32_t instance, uint32_t offset,
                       uint32_t pitch, uint32_t width, uint32_t height, int zeta, target *target)
{
    h2_dma_object dma;
    if (!pitch || (pitch & 3) || (offset & 3) || (uint64_t)width * 4 > pitch ||
        !h2_dma_load(state->read_instance, state->opaque, instance, &dma)) return 0;
    uint64_t bytes = (uint64_t)(height - 1) * pitch + width * 4;
    if (bytes > UINT32_MAX || !h2_dma_resolve(&dma, offset, (uint32_t)bytes, 1,
                                            state->physical_bytes, &target->physical)) return 0;
    target->bytes = bytes; target->pitch = pitch;
    if (state->check_attachment && !state->check_attachment(state->opaque, target->physical,
                                    target->bytes, pitch, zeta, state->format)) return 0;
    if (!zeta && h2_menu_gxm_before_cpu_write) h2_menu_gxm_before_cpu_write(target->physical, (uint32_t)bytes, g_whole_surface_clear);
    target->data = state->map_physical(state->opaque, target->physical, target->bytes);
    return target->data != NULL && target->bytes <= UINTPTR_MAX - (uintptr_t)target->data;
}

/* A GPU-side menu backend (menu_gxm.c) may hold un-downloaded draws; the game's clears
 * write the guest buffers, so it must land its pending scene first and learn about
 * depth clears. Weak: absent in the default state-only build and the host tests. */
extern void h2_menu_gxm_flush(void) __attribute__((weak));
extern void h2_menu_gxm_zeta_cleared(uint32_t clear_value) __attribute__((weak));
static int clear_surface(h2_kelvin_clear *state, uint32_t flags)
{
    if (flags & ~0xF3u) return 0;
    if (!flags) return 1;
    if ((flags & 3) && h2_menu_gxm_zeta_cleared) h2_menu_gxm_zeta_cleared(state->clear_zstencil);
    /* First supported shape: origin-zero pitch surfaces without multisampling.
     * Restrict the format to ARGB8/Z24S8; other layouts need their own consumer. */
    if ((state->format & 0xFFFFu) != 0x128u ||
        ((state->format >> 16) & 255) > 12 || (state->format >> 24) > 12 ||
        (state->clip_horizontal & 0xFFFF) || (state->clip_vertical & 0xFFFF)) return 0;
    uint32_t width = state->clip_horizontal >> 16, height = state->clip_vertical >> 16;
    uint32_t xmin = state->clear_horizontal & 0xFFF, xmax = (state->clear_horizontal >> 16) & 0xFFF;
    uint32_t ymin = state->clear_vertical & 0xFFF, ymax = (state->clear_vertical >> 16) & 0xFFF;
    if (!width || !height || width > 4096 || height > 4096 ||
        xmin > xmax || ymin > ymax || xmax >= width || ymax >= height) return 0;
    g_whole_surface_clear = !xmin && !ymin && xmax == width - 1 && ymax == height - 1 && (flags & 0xF0) == 0xF0;
    target targets[2] = {{0}}; unsigned count = 0;
    if (flags & 0xF0) {
        if (!state->has_color_dma || !map_target(state, state->dma_color, state->color_offset,
                                                state->pitch & 0xFFFF, width, height, 0, &targets[count])) return 0;
        targets[count].mask = ((flags & 0x10) ? 0x00FF0000 : 0) | ((flags & 0x20) ? 0x0000FF00 : 0) |
                              ((flags & 0x40) ? 0x000000FF : 0) | ((flags & 0x80) ? 0xFF000000u : 0);
        targets[count++].value = state->clear_color;
    }
    if (flags & 3) {
        if (!state->has_zeta_dma || !map_target(state, state->dma_zeta, state->zeta_offset,
                                               state->pitch >> 16, width, height, 1, &targets[count])) return 0;
        targets[count].mask = ((flags & 1) ? 0xFFFFFF00u : 0) | ((flags & 2) ? 0xFF : 0);
        targets[count++].value = state->clear_zstencil;
    }
    /* Aliased attachments need separately established ordering semantics. */
    if (count == 2 && (uint64_t)targets[0].physical < (uint64_t)targets[1].physical + targets[1].bytes &&
        (uint64_t)targets[1].physical < (uint64_t)targets[0].physical + targets[0].bytes) return 0;
    if (count == 2 && (uintptr_t)targets[0].data < (uintptr_t)targets[1].data + targets[1].bytes &&
        (uintptr_t)targets[1].data < (uintptr_t)targets[0].data + targets[0].bytes) return 0;
    for (unsigned i = 0; i < count; ++i) {
        target *t = &targets[i];
        for (uint32_t y = ymin; y <= ymax; ++y) {
            for (uint32_t x = xmin; x <= xmax; ++x) {
                uint8_t *pixel = t->data + y * t->pitch + x * 4;
                uint32_t prior = (uint32_t)pixel[0] | (uint32_t)pixel[1] << 8 |
                                 (uint32_t)pixel[2] << 16 | (uint32_t)pixel[3] << 24;
                uint32_t result = (prior & ~t->mask) | (t->value & t->mask);
                for (unsigned byte = 0; byte < 4; ++byte) pixel[byte] = result >> (byte * 8);
            }
        }
    }
    ++state->completed_clears;
    state->written_pixels += (uint64_t)(xmax - xmin + 1) * (ymax - ymin + 1) * count;
    return 1;
}

int h2_kelvin_clear_method(void *opaque, uint8_t subchannel, uint16_t method,
                           uint32_t value, uint32_t source_address)
{
    (void)source_address;
    h2_kelvin_clear *state = opaque;
    if (!state || !state->read_instance || !state->map_physical || subchannel > 7 || (method & 3)) return 0;
    if (method == 0) {
        h2_gpu_object object; uint32_t context;
        if (!h2_object_lookup(state->read_instance, state->opaque, value, state->channel, &object) ||
            object.engine != 1 || !state->read_instance(state->opaque, object.instance, &context) ||
            (context & 0xFFF) != 0x97 || (state->has_object && state->object_instance != object.instance)) return 0;
        state->object_instance = object.instance; state->has_object = 1;
        state->bound_subchannels |= 1u << subchannel;
        return 1;
    }
    if (!(state->bound_subchannels & (1u << subchannel))) return 0;
    uint32_t instance;
    switch (method) {
    case 0x194:
        if (!lookup_dma(state, value, &instance)) return 0;
        state->dma_color = instance; state->has_color_dma = 1; return 1;
    case 0x198:
        if (!lookup_dma(state, value, &instance)) return 0;
        state->dma_zeta = instance; state->has_zeta_dma = 1; return 1;
    case 0x200: state->clip_horizontal = value; return 1;
    case 0x204: state->clip_vertical = value; return 1;
    case 0x208: state->format = value; return 1;
    case 0x20C: state->pitch = value; return 1;
    case 0x210: state->color_offset = value; return 1;
    case 0x214: state->zeta_offset = value; return 1;
    case 0x1D8C: state->clear_zstencil = value; return 1;
    case 0x1D90: state->clear_color = value; return 1;
    case 0x1D94: return clear_surface(state, value);
    case 0x1D98:
        if (value & 0xF000F000u) return 0;
        state->clear_horizontal = value; return 1;
    case 0x1D9C:
        if (value & 0xF000F000u) return 0;
        state->clear_vertical = value; return 1;
    default: return 0;
    }
}
