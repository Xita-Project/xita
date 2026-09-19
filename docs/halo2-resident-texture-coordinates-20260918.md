# Halo 2 resident render-target coordinates

The direct render-target texture path in `games/halo2_5849/menu_gxm.c`
skipped coordinate normalisation. `sampled_target` accepts only an exact
640×480 linear ARGB8 view (format 0x12, pitch 2560, matching buffer offset).
It binds that surface without creating a texture-cache entry. The later
uniform setup checked only the cache entry's `linear` flag, so the resident
path received `(1, 1)` while the equivalent copied image received
`(1/640, 1/480)`.

`tools/h2_menu_shaders.py` enables `TEXCOORD_SCALE`; the fragment generator
multiplies the sampling coordinates by `xv_texscale`. For example, the Xbox
texel centre `(320.5, 240.5)` needs to become approximately `(0.5008, 0.5010)`.
Passing the original values to a clamped normalised sampler addresses an edge
instead. This can corrupt full-screen composition even when geometry and the
source surface are correct.

Bindings with a resident surface and no cache entry now receive the same
normalisation as the copied path. Normalised textures, cubes, other linear
image sizes, and unused units retain their existing scales. The fix adds no
readback, allocation, shader regeneration, or synchronization.

## Evidence and validation

- The archived private `native-mp328j-artifacts/boot.log` reports 900 resident
  texture bindings over the final 60-flip window (serial 2160). This is an
  exercised path, not just hypothetical code. That emulator evidence does not
  establish hardware frame rate or complete menu correctness.
- `python3 tools/test_halo2_rtt_scale.py` extracts the actual texture binding
  loop and scale setup. Its independent texel-centre oracle fails on the old
  resident path, then passes with the correction.
- `SANITIZE=1 python3 tools/test_halo2_rtt_scale.py` passes with ASan/UBSan.
  Cases cover all four resident units, copied views, mixed image types/sizes,
  unused and missing bindings, consecutive-draw state reset, and the existing
  same-target copy fallback. The resident case checks that no finish, download,
  or texture-cache fetch is introduced. GXM functions are host doubles; this
  does not prove GPU ordering or the final displayed image.

The old native106/native213 startup notes describe earlier milestones. The
mp328 lineage used for the combined package reaches extensive GXM submission;
visible main-menu and physical hardware behavior still need direct validation.
This fix has not yet been visually validated or deployed to the Vita.
