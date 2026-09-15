#pragma once
#include "linear_texture.h"

typedef struct h2_sprite_vertex { float attribute[3][4]; } h2_sprite_vertex;
/* Owned version1 reference pins the compiled shader and fixed projection.
 * Captured inline words are evidence only, never a replacement vertex source. */
typedef struct h2_sprite_contract {
    uint32_t setup[2048], setup_valid[64], program[21][4], constants[178][4];
    uint32_t captured_inline[20];
} h2_sprite_contract;
_Static_assert(sizeof(h2_sprite_vertex) == 48, "sprite vertex layout");
_Static_assert(sizeof(h2_sprite_contract) == 11712, "private sprite contract layout");
typedef struct h2_sprite_request {
    h2_block_texture texture0;
    const uint8_t *destination;
    h2_sprite_vertex vertices[4];
    float factors[18][4];
    uint32_t constants[178][4];
} h2_sprite_request;
/* Return a completed independent 640x480 ARGB8 staging image or NULL.
 * The callback must not mutate any guest source, destination or request. */
typedef const uint32_t *(*h2_sprite_render)(void *, const h2_sprite_request *);
typedef struct h2_sprite_draw {
    uint32_t words[20], count;
    uint64_t completed;
    uint8_t active, subchannel;
    const h2_sprite_contract *contract;
    h2_sprite_render render;
    void *opaque;
} h2_sprite_draw;
/* Bounded float2/float2/UB_D3D inline rectangle, exact owned program/state,
 * full normalized UV corners or four positive-zero pairs, uniform packed color,
 * one-level BC2 <=1024 per axis and <=8192 bytes. RGB-only commit after END
 * revalidation and successful staging; rejection preserves guest/state. */
int h2_sprite_method(h2_sprite_draw *, h2_command_state *, h2_kelvin_clear *,
                      uint8_t subchannel, uint16_t method, uint32_t value);
