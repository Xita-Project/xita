/* Audited constructor state only. Draw, blit, notify, flip and report execution
 * remain unsupported. Stored state is preserved even while those paths reject. */
#pragma once
#include "kelvin_clear.h"
typedef struct h2_command_object {
    uint32_t instance, context[4];
    uint8_t present;
} h2_command_object;
typedef struct h2_command_state {
    h2_command_object objects[5]; /* Kelvin, M2MF, blit, surfaces2D, pattern */
    uint8_t bound[8]; /* object index plus one; zero is unbound */
    uint32_t dma[11], dma_valid;
    uint32_t m2mf_notifier, surfaces_dma[2], blit_context[7], blit_operation, pattern_color;
    uint32_t constants[192][4], constant_load;
    uint32_t semaphore_offset, flip_read, flip_write, flip_modulo;
    uint32_t provoking_vertex, edge_flag, compress_depth, shader_inputs, shadow_slope;
    uint64_t semaphore_releases;
    uint32_t last_semaphore_address, last_semaphore_value;
} h2_command_state;
/* Rejection preserves command/clear state and guest bytes. */
int h2_command_method(h2_command_state *state, h2_kelvin_clear *clear,
                       uint8_t sub, uint16_t method, uint32_t value, uint32_t source);
