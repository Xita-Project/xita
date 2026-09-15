#pragma once
#include "linear_texture.h"

typedef struct h2_blend_vertex { float attribute[7][4]; } h2_blend_vertex;
/* Private version1 contract: owned program/state plus the bounded observed
 * immediate inputs. It is never an alternate source of emitted vertices. */
typedef struct h2_blend_contract {
    uint32_t setup[2048], setup_valid[64], program[7][4];
    h2_blend_vertex vertices[4];
} h2_blend_contract;
_Static_assert(sizeof(h2_blend_vertex) == 112, "blend vertex layout");
_Static_assert(sizeof(h2_blend_contract) == 9008, "private blend contract layout");
typedef struct h2_blend_request {
    h2_linear_texture texture0;
    const uint8_t *destination;
    h2_blend_vertex vertices[4];
    float factors[18][4];
} h2_blend_request;
/* Render into independent private staging. Return completed 640x480 ARGB8
 * pixels or NULL; never mutate the request or any guest input/destination. */
typedef const uint32_t *(*h2_blend_render)(void *, const h2_blend_request *);
typedef struct h2_blend_draw {
    h2_blend_vertex vertices[4];
    uint32_t vertex, phase;
    uint64_t completed;
    uint8_t active, subchannel;
    const h2_blend_contract *contract;
    h2_blend_render render;
    void *opaque;
} h2_blend_draw;
/* Only observed immediate quads. All rejection preserves state/guest memory.
 * END revalidates the entire contract and resource spans before rendering;
 * guest RGBA is committed only after successful completed staging output. */
int h2_blend_method(h2_blend_draw *, h2_command_state *, h2_kelvin_clear *,
                      uint8_t subchannel, uint16_t method, uint32_t value);

