/* Software NV2A vertex-program interpreter.
 *
 * The menu draws are transformed by the game's uploaded NV2A vertex program
 * (state->program: one 128-bit / 4-DWORD slot per instruction, dual-issuing one
 * MAC and one ILU op). Rather than translate each program variant to a GPU
 * vertex shader, this runs the microcode in software per vertex; GXM then only
 * rasterizes the already-transformed vertices with the combiner fragment shader.
 *
 * Encoding matches recompiler/dx8_shader_parse.py (the project's offline decoder):
 * same field table, MAC/ILU opcode maps, the ADD-reads-A-and-C quirk, r1-hardwired
 * ILU temp write, r12 as a readable alias of oPos, and a0.x-relative constants.
 * Straight-line only (this ISA has no branches); execution runs slots in order. */
#pragma once
#include <stdint.h>

enum {
    NV2A_VSH_TEMPS = 16,   /* r0..r11 (r12 aliases oPos) */
    NV2A_VSH_INPUTS = 16,  /* v0..v15 vertex attributes */
    NV2A_VSH_CONSTS = 192, /* c0..c191 */
    NV2A_VSH_OUTPUTS = 16  /* oPos=0, oD0=3, oD1=4, oFog=5, oPts=6, oB0=7, oB1=8, oT0=9..oT3=12 */
};
enum {
    NV2A_O_POS = 0, NV2A_O_D0 = 3, NV2A_O_D1 = 4, NV2A_O_FOG = 5, NV2A_O_PTS = 6,
    NV2A_O_B0 = 7, NV2A_O_B1 = 8, NV2A_O_T0 = 9, NV2A_O_T1 = 10, NV2A_O_T2 = 11, NV2A_O_T3 = 12
};

/* Run `count` slots starting at `start`. input holds v0..v15, constant c0..c191.
 * output receives every written o-register (unwritten ones are left at 0, except
 * oPos/oD0 which are pre-seeded so a program that omits them is still defined).
 * Returns the number of slots executed (stops after a slot with the FINAL bit). */
unsigned nv2a_vsh_run(const uint32_t (*program)[4], unsigned start, unsigned count,
                      const float input[NV2A_VSH_INPUTS][4],
                      const float constant[NV2A_VSH_CONSTS][4],
                      float output[NV2A_VSH_OUTPUTS][4]);
