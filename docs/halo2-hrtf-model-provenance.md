# Fixed Halo 2 HRTF model provenance

`games/halo2_5849/audio_hrtf_model.h` adapts the symmetric, zero-delay subset
of xemu's [HRTF filter](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/vp/hrtf.h),
commit `75650bd8cd91945f7b79774e2cee0b200ca373ff`. The original copyright and
LGPL-2.0-or-later notice are retained, with the license in
`games/halo2_5849/COPYING.HRTF.LIB`. No Xbox filter coefficients are included.

The fixed subset uses one 31-sample history because both coefficient sets are
identical and the interaural delay is zero. It retains the reference's tap
normalization, per-sample parameter smoothing and accumulation order. It restores
the caller's floating-point environment and converts to GP signed-24 input
with the reference's nearest rounding and clipping. Other positions, delays,
filters, gain controls and dynamic parameter changes are unsupported.

This is an explicit emulator model. In particular, xemu's smoothing rate is not
established MCPX transition timing. The opt-in `AUDIO_SPATIAL_MODEL=1` build
requires `AUDIO_DSP=1`; the default remains disabled. The consumer feeds actual
GP inputs and output processing; filter adoption alone does not establish
real-time audio, a visible main menu, or hardware accuracy.
