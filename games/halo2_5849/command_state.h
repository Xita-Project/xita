/* Audited command state and bounded synchronous clear/semaphore/frame copy.
 * Other execution requires an explicit consumer or rejects. */
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
    uint32_t program[136][4], program_load, program_start, execution_mode, context_write;
    uint32_t vertex4ub[16]; /* packed attribute inputs; no vertices are emitted */
    /* Original inputs for an explicit whitelist of non-executing setup methods.
     * This is not a complete PGRAPH register file or an implemented draw state. */
    uint32_t setup[0x2000 / 4], setup_valid[0x2000 / 4 / 32];
    uint32_t semaphore_offset, flip_read, flip_write, flip_modulo;
    uint32_t provoking_vertex, edge_flag, compress_depth, shader_inputs, shadow_slope;
    /* XDK 5849 software-method inputs, not physical RDI/register emulation.
     * Future texture/draw execution must honor these settings or reject. */
    uint32_t dxt1_noise, zcull_debug5, rop_control, software_valid;
    uint64_t software_updates;
    uint64_t semaphore_releases;
    uint32_t last_semaphore_address, last_semaphore_value;
    uint32_t surfaces_format, surfaces_pitch, surfaces_offset[2], surfaces_valid;
    uint32_t blit_point[2], blit_point_valid;
    uint64_t completed_blits, copied_bytes;
    uint32_t last_blit_source, last_blit_dest;
} h2_command_state;
/* Rejection preserves command/clear state and guest bytes. */
int h2_command_method(h2_command_state *state, h2_kelvin_clear *clear,
                       uint8_t sub, uint16_t method, uint32_t value, uint32_t source);
