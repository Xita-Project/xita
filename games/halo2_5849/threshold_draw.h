#pragma once
#include "linear_texture.h"

typedef struct h2_threshold_vertex { float attribute[7][4]; } h2_threshold_vertex;
/* Private version1 contract: owned program/state and exact observed
 * 24-word vertex packets. Every accepted vertex comes from live commands;
 * the contract is not a source of emitted vertices. */
typedef struct h2_threshold_contract {
    uint32_t setup[2048], setup_valid[64], program[7][4];
    h2_threshold_vertex vertices[4];
} h2_threshold_contract;
_Static_assert(sizeof(h2_threshold_vertex) == 112, "threshold vertex layout");
_Static_assert(sizeof(h2_threshold_contract) == 9008, "private threshold contract layout");
typedef struct h2_threshold_request {
    h2_linear_texture textures[4];
    const uint8_t *destination;
    h2_threshold_vertex vertices[4];
    float factors[18][4];
} h2_threshold_request;
/* Render into independent private staging. Return completed 160x120 ARGB8
 * pixels or NULL; never mutate the request or any guest input/destination. */
typedef const uint32_t *(*h2_threshold_render)(void *, const h2_threshold_request *);
typedef struct h2_threshold_draw {
    h2_threshold_vertex vertices[4];
    uint32_t vertex, phase;
    uint64_t completed;
    uint8_t active, subchannel;
    const h2_threshold_contract *contract;
    h2_threshold_render render;
    void *opaque;
} h2_threshold_draw;
/* Only observed immediate quads. All rejection preserves state/guest memory.
 * END revalidates the entire contract and resource spans before rendering;
 * guest RGBA is committed only after successful completed staging output. */
int h2_threshold_method(h2_threshold_draw *, h2_command_state *, h2_kelvin_clear *,
                      uint8_t subchannel, uint16_t method, uint32_t value);
