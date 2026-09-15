#pragma once
#include "linear_texture.h"

typedef struct h2_bc1_vertex { float attribute[7][4]; } h2_bc1_vertex;
/* Private version1 contract: owned program/state plus the bounded observed
 * position inputs. Texture coordinates remain live commands, never fixture
 * substitution. The contract is not a source of emitted vertices. */
typedef struct h2_bc1_contract {
    uint32_t setup[2048], setup_valid[64], program[7][4];
    h2_bc1_vertex vertices[4];
} h2_bc1_contract;
_Static_assert(sizeof(h2_bc1_vertex) == 112, "BC1 vertex layout");
_Static_assert(sizeof(h2_bc1_contract) == 9008, "private BC1 contract layout");
typedef struct h2_bc1_request {
    h2_block_texture textures[4];
    const uint8_t *destination;
    h2_bc1_vertex vertices[4];
    float factors[18][4];
} h2_bc1_request;
/* Render into independent private staging. Return completed 320x240 ARGB8
 * pixels or NULL; never mutate the request or any guest input/destination. */
typedef const uint32_t *(*h2_bc1_render)(void *, const h2_bc1_request *);
typedef struct h2_bc1_draw {
    h2_bc1_vertex vertices[4];
    uint32_t vertex, phase;
    uint64_t completed;
    uint8_t active, subchannel;
    const h2_bc1_contract *contract;
    h2_bc1_render render;
    void *opaque;
} h2_bc1_draw;
/* Only observed immediate quads. All rejection preserves state/guest memory.
 * END revalidates the entire contract and resource spans before rendering;
 * guest RGBA is committed only after successful completed staging output. */
int h2_bc1_method(h2_bc1_draw *, h2_command_state *, h2_kelvin_clear *,
                      uint8_t subchannel, uint16_t method, uint32_t value);
