/* NV2A register-combiner evaluator for the software menu backend.
 *
 * Model and bit layout ported from recompiler/dx8_pixelshader_parse.py and
 * pixelshader_recomp_gen.py (the project's own combiner tooling): up to 8 general
 * stages plus a final combiner. Each stage combiner input byte is
 * register(0-3) | channel(bit4) | mapping(bits5-7); the output word is
 * cd(0-3) | ab(4-7) | sum(8-11) | flags(12-19: dot/mux/scale). The final combiner
 * computes rgb = A*B + (1-A)*C + D and alpha = G. Evaluated per fragment against
 * the interpolated diffuse/specular and the four sampled textures. */
#pragma once
#include "command_state.h"

typedef struct menu_combiner {
    uint32_t rgb_in[8], rgb_out[8], alpha_in[8], alpha_out[8];
    uint32_t factor0[8], factor1[8];   /* per-stage c0/c1, ARGB packed */
    uint32_t final_factor0, final_factor1; /* final combiner c0/c1: SET_SPECULAR_FOG_FACTOR0/1 */
    uint32_t final_abcd, final_efg;
    unsigned stages;
    int mux_msb;
    /* Filled by menu_combiner_prepare (decode does it): unpacked constants and
     * the set of texture units the program reads, so the per-fragment path
     * neither unpacks ARGB words nor needs samples from unused units. */
    int prepared;
    float c0f[8][4], c1f[8][4], final_c0f[4], final_c1f[4];
    unsigned tex_used;                 /* bit i => texture unit i is an input */
} menu_combiner;

void menu_combiner_prepare(menu_combiner *cb);

/* Decode the combiner configuration from captured command state. */
void menu_combiner_decode(const h2_command_state *state, menu_combiner *cb);

/* Evaluate one fragment. tex[i] is the RGBA (0..1) sample from texture unit i
 * (or {0,0,0,0} if that unit is unused); diffuse/specular are oD0/oD1; fog is
 * the fog colour/factor. out receives the final RGBA (0..1). */
void menu_combiner_eval(const menu_combiner *cb, const float tex[4][4],
                        const float diffuse[4], const float specular[4],
                        const float fog[4], float out[4]);
