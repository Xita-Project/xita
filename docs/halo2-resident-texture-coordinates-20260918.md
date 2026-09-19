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
The VitaSDK candidate built successfully, changing only the executable in the
Halo 2 package. A combined updater candidate preserves CE, the launcher, and
every packaged shader/asset; its only differences are `halo2-a.self` and the
corresponding boot record. It has not been deployed to the Vita.

## Subsequent visual comparison

Both the installed-lineage baseline (`711d40d1…`) and corrected candidate
(`34dcd68e…`) reached the original profile-selection and main-menu screens in
isolated Vita3K sessions. Normal Start skipped the intro and Cross selected the
existing Default profile. Text is readable and menu input responds in both.
Both still show substantial corruption in the animated background. These
different-time screenshots cannot establish a visual improvement from the
coordinate fix; main-menu startup already worked before this change.

Private evidence is under `halo2-rtt-scale/visual-{baseline,fixed}` beside the
prepared build, with screenshots, executable/log hashes, input event receipts,
and terminal run records. The runs used the same game data, original profile
files, fresh cache4 namespaces, software OpenGL configuration, and packaged
shaders. Only the runtime and its revision differ. Each emulator was stopped
after capture; neither is a hardware performance measurement.

The user's first physical Halo 2 launch remained black. Its log has not yet
been collected. Before guest startup, `h2_cache_mounts` extends three backing
files to 750 MiB each. Desktop sparse-file behavior may conceal a significant
first-launch storage cost on the Vita, but this is a hypothesis until the device
log/cache state identifies the stopping point.
