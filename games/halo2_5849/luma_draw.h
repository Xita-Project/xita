#pragma once
#include "linear_texture.h"

typedef struct h2_luma_vertex { float attribute[8][4]; } h2_luma_vertex;
/* Private version1 contract: owned program/state plus the bounded observed
 * immediate inputs. It is never an alternate source of emitted vertices. */
typedef struct h2_luma_contract {
    uint32_t setup[2048], setup_valid[64], program[12][4], constants[8][4];
    h2_luma_vertex vertices[4];
} h2_luma_contract;
_Static_assert(sizeof(h2_luma_vertex) == 128, "luma vertex layout");
_Static_assert(sizeof(h2_luma_contract) == 9280, "private luma contract layout");
typedef struct h2_luma_request {
    h2_linear_texture texture0;
    const uint8_t *destination;
    h2_luma_vertex vertices[4];
    float factors[18][4];
    uint32_t constants[8][4];
} h2_luma_request;
/* Render into independent private staging. Return completed 640x480 ARGB8
 * pixels or NULL; never mutate the request or any guest input/destination. */
typedef const uint32_t *(*h2_luma_render)(void *, const h2_luma_request *);
typedef struct h2_luma_draw {
    h2_luma_vertex vertices[4];
    uint32_t vertex, phase;
    uint64_t completed;
    uint8_t active, subchannel;
    const h2_luma_contract *contract;
    h2_luma_render render;
    void *opaque;
} h2_luma_draw;
/* Only the observed immediate triangle strip. All rejection preserves state/guest memory.
 * END revalidates the entire contract and resource spans before rendering;
 * guest RGBA is committed only after successful completed staging output. */
int h2_luma_method(h2_luma_draw *, h2_command_state *, h2_kelvin_clear *,
                      uint8_t subchannel, uint16_t method, uint32_t value);
