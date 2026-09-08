/* Halo HUD combiner recognition. Unused NV2A stages retain earlier material
 * state, so hashing the complete register file is not a stable shader key.
 * Most HUD/radar outputs read texture 0; scope markings also use texture 1. Compare every
 * active combiner word, final output and control flag; keep constants dynamic.
 */
#pragma once
#include <stdint.h>
#include "xv_ps_key.h"

/* These screen-space composites need all of their sampled textures and
 * alpha/constant state even when none of the inputs aliases the backbuffer. */
static uint32_t xv_composite_program_for_vs(uint32_t vs, const uint32_t *def)
{
    if (vs != 0xBB2F446Bu || !def) return 0;
    switch (xv_ps_program_key(def)) {
    case 0x1F88B236u: return 0x1F88B236u; /* eight-stage cinematic composite */
    case 0x4ECE4699u: return 0x423B087Du; /* four-texture screen effect */
    case 0x51622122u: return 0x1E073CA3u;
    case 0x63C01584u: return 0x74D0C65Eu;
    case 0x7377025Au: return 0xB8A3D35Fu; /* loading variant with dynamic constant */
    case 0xD24A1DC6u: return 0x8C25D9E4u; /* existing four-texture composite */
    default: return 0;
    }
}

/* VS38 emits each stage's UV through two independent DPH rows. Xbox linear
 * texture coordinates are pixels; divide the entire output row, including its
 * offset, without modifying the guest constants or normalized/swizzled inputs. */
static void xv_composite_texture_rows(float out[8][4], const float in[8][4],
                                      const uint32_t sizes[4])
{
    memcpy(out,in,8*4*sizeof(float));
    for (unsigned t=0;t<4;t++) if (sizes[t]) {
        float w=(float)((sizes[t]&0xFFFu)+1u);
        float h=(float)(((sizes[t]>>12)&0xFFFu)+1u);
        for (unsigned j=0;j<4;j++) { out[2*t][j]/=w; out[2*t+1][j]/=h; }
    }
}

static uint32_t xv_hud_program(const uint32_t *def)
{
    static const struct {
        uint32_t hash, control, final[2], stage[4][4];
    } programs[] = {
        /* Radar producer: texture times vertex color. Radar composite: texture
         * only, including alpha. Both use the existing runtime fragment programs. */
        {0x6C94962Bu, 1u, {0x0000000Cu, 0x00001C00u}, {
            {0x18140000u, 0x000000C0u, 0x08040000u, 0x000000C0u}}},
        {0x1A42D493u, 1u, {0x00000008u, 0x00001800u}, {{0, 0, 0, 0}}},
        /* Motion blip: texture RGB times vertex RGB, with ZERO alpha.
         * The font path's texture alpha makes the radar composite opaque. */
        {0xC2D57121u, 1u, {0x08040000u, 0x00000000u}, {{0, 0, 0, 0}}},
        {0xA972FE61u, 0x00011102u, {0x0000000Cu, 0x00001C00u}, {
            {0x18111912u, 0x00000089u, 0x08010902u, 0x00000089u},
            {0x1A111814u, 0x000000ACu, 0x0A010804u, 0x000000ACu}}},
        /* Scope markings multiply the ordinary HUD output by texture 1.
         * Selecting only t1 discards the texture-0 mask and dynamic constants. */
        {0xFFBC5A4Du, 0x00011103u, {0x0000000Cu, 0x00001C00u}, {
            {0x18111912u, 0x00000089u, 0x08010902u, 0x00000089u},
            {0x1A111814u, 0x000000ACu, 0x0A010804u, 0x000000ACu},
            {0x1C190000u, 0x000000C0u, 0x0C090000u, 0x000000C0u}}},
        {0x5D70F0B3u, 0x00011104u, {0x0C180000u, 0x00001C00u}, {
            {0x12081208u, 0x00020C00u, 0x1120E820u, 0x00020C00u},
            {0x6C200000u, 0x000000C0u, 0x3C011C02u, 0x00000C00u},
            {0x0820B220u, 0x00000C00u, 0x0C201C02u, 0x00000C00u},
            {0x12201120u, 0x00004C00u, 0x0C200120u, 0x00004C00u}}},
    };
    if (!def || (def[0xD8 / 4] & 31u) != 1u) return 0; /* t0 PROJECT2D */
    for (unsigned t = 1; t < 4; t++) {
        unsigned mode = (def[0xD8 / 4] >> (5 * t)) & 31u;
        /* Unused ordinary samples are harmless. A clip stage can discard the
         * fragment independently of the combiners, so reject it and unknown modes. */
        if (mode != 0 && mode != 1 && mode != 3) return 0;
    }
    for (unsigned p = 0; p < sizeof programs / sizeof programs[0]; p++) {
        if (programs[p].hash == 0xFFBC5A4Du && ((def[0xD8 / 4] >> 5) & 31u) != 1u)
            continue; /* this program samples t1 PROJECT2D */
        if (def[0xD4 / 4] != programs[p].control ||
            def[8] != programs[p].final[0] || def[9] != programs[p].final[1]) continue;
        unsigned n = programs[p].control & 15u, i;
        for (i = 0; i < n; i++)
            if (def[i] != programs[p].stage[i][0] || def[0x68 / 4 + i] != programs[p].stage[i][1] ||
                def[0x88 / 4 + i] != programs[p].stage[i][2] || def[0xB4 / 4 + i] != programs[p].stage[i][3]) break;
        if (i == n) return programs[p].hash;
    }
    return 0;
}

/* Recognition alone does not guarantee that the mesh path has a shader for
 * this vertex program. VS04 menu glyphs can share a HUD combiner; their existing
 * coverage path must remain in use until that complete shader pair is supported. */
static uint32_t xv_hud_program_for_vs(uint32_t vs, const uint32_t *def)
{
    if (vs != 0x1DAF0284u && vs != 0x4469E1F8u) return 0;
    uint32_t ps = xv_hud_program(def);
    if (ps == 0xC2D57121u) return vs == 0x4469E1F8u ? ps : 0;
    if (vs == 0x1DAF0284u || ps == 0x6C94962Bu || ps == 0x1A42D493u) return ps;
    return 0;
}
