/* Small synchronous host consumer: one Kelvin object, linear ARGB8/Z24S8 clear.
 * This is deliberately not a general graphics state machine or draw backend. */
#pragma once
#include "gpu_objects.h"

typedef void *(*h2_physical_map)(void *opaque, uint32_t address, uint32_t bytes);
typedef struct h2_kelvin_clear {
    h2_instance_read read_instance;
    h2_physical_map map_physical;
    void *opaque;
    uint32_t physical_bytes, object_instance, bound_subchannels;
    uint32_t dma_color, dma_zeta;
    uint32_t clip_horizontal, clip_vertical, format, pitch, color_offset, zeta_offset;
    uint32_t clear_color, clear_zstencil, clear_horizontal, clear_vertical;
    uint64_t completed_clears, written_pixels;
    uint8_t channel, has_object, has_color_dma, has_zeta_dma;
} h2_kelvin_clear;
/* map_physical must validate the whole requested RAM span before returning a
 * stable writable pointer, or return NULL without mutation. The consumer checks
 * both physical and returned host spans for overlap; injectivity is not assumed. */
int h2_kelvin_clear_init(h2_kelvin_clear *state, h2_instance_read read_instance,
                         h2_physical_map map_physical, void *opaque,
                         uint32_t physical_bytes, uint8_t channel);
/* Signature matches the push parser consumer. Rejection does not mutate state
 * or framebuffer bytes. Unknown classes/methods, swizzle/MSAA and draws reject. */
int h2_kelvin_clear_method(void *opaque, uint8_t subchannel, uint16_t method,
                           uint32_t value, uint32_t source_address);
