#pragma once
#include "linear_texture.h"

typedef struct h2_screen_vertex { float attribute[7][4]; } h2_screen_vertex;
/* Private version2 contract: owned program/state plus the bounded observed
 * immediate inputs. It is never an alternate source of emitted vertices. */
typedef struct h2_screen_contract {
    uint32_t setup[2048], setup_valid[64], program[7][4];
    h2_screen_vertex vertices[4];
} h2_screen_contract;
_Static_assert(sizeof(h2_screen_vertex) == 112, "screen vertex layout");
_Static_assert(sizeof(h2_screen_contract) == 9008, "private screen contract layout");
typedef struct h2_screen_request {
    h2_linear_texture texture0;
    h2_block_texture texture2;
    const uint8_t *destination;
    h2_screen_vertex vertices[4];
    float factors[18][4];
} h2_screen_request;
/* Render into independent private staging. Return completed 640x480 ARGB8
 * pixels or NULL; never mutate the request or any guest input/destination. */
typedef const uint32_t *(*h2_screen_render)(void *, const h2_screen_request *);
typedef struct h2_screen_draw {
    h2_screen_vertex vertices[4];
    uint32_t vertex, phase;
    uint64_t completed;
    uint8_t active, subchannel;
    const h2_screen_contract *contract;
    h2_screen_render render;
    void *opaque;
} h2_screen_draw;
/* Only observed immediate quads. All rejection preserves state/guest memory.
 * END revalidates the entire contract and resource spans before rendering;
 * guest RGBA is committed only after successful completed staging output. */
int h2_screen_method(h2_screen_draw *, h2_command_state *, h2_kelvin_clear *,
                      uint8_t subchannel, uint16_t method, uint32_t value);
