/* First observed movie quad only. Unknown pipeline/geometry rejects. */
#pragma once
#include "linear_texture.h"

/* Trusted build-time reference, emitted only from the pinned native73 capture.
 * Program bytes/constants remain private alongside the owned game image. */
typedef struct h2_quad_contract {
    uint32_t setup[2048], setup_valid[64];
    uint32_t constants[178][4], program[21][4];
} h2_quad_contract;
typedef struct h2_quad_vertex { float position[4], texcoord[4], diffuse[4]; } h2_quad_vertex;
_Static_assert(sizeof(h2_quad_contract) == 11632, "private quad contract layout");
_Static_assert(sizeof(h2_quad_vertex) == 48, "GXM expanded vertex layout");
typedef struct h2_quad_request {
    h2_linear_texture texture;
    h2_quad_vertex vertices[4];
    const uint32_t (*constants)[4];
} h2_quad_request;
/* Return a complete private 640x480 RGB staging image after GPU completion.
 * On failure return NULL. Never mutate guest memory or the request. */
typedef const uint32_t *(*h2_quad_render)(void *opaque, const h2_quad_request *request);
typedef struct h2_quad_draw {
    h2_quad_vertex vertices[4];
    uint32_t vertex, phase;
    uint64_t completed;
    uint8_t active, subchannel;
    const h2_quad_contract *contract;
    h2_quad_render render;
    void *opaque;
} h2_quad_draw;

/* Call only for Kelvin methods while active, or for BEGIN/END. Any rejected
 * method leaves state and guest bytes unchanged. END commits RGB only after
 * full state/resource/geometry validation and successful private rendering. */
int h2_quad_method(h2_quad_draw *draw, h2_command_state *state,
                   h2_kelvin_clear *memory, uint8_t subchannel,
                   uint16_t method, uint32_t value);
