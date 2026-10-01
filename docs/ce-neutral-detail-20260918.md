# Halo CE constant-detail shader experiment — September 18

`XV_NEUTRAL_DETAIL=1` enables a GPU-work optimization for material family
`154066FD`. It defaults off. No tester VPK or device files were changed.

The saved September 7 Blood Gulch trace repeatedly binds a 4×4 RGBA texture at
stage 1 for this family. The trace does not establish its pixel color. The new
runtime proof checks actual uploaded pixels before selecting a shader that
replaces this sample with `(128/255, 128/255, 128/255, 1)`.

## Work removed

Eighteen SDK-compiled variants cover six shader paths and generic, alpha-disabled,
and GREATER alpha modes. Source shaders go from four texture fetch expressions
to three. Compiler reflection confirms removal of `tex1`: 0D variants go from
three active samplers to two (tex3 was already unused); 3F/7F go from four to
three. This is reduced fragment texture work, not a measured FPS gain.

All other source arithmetic, precision declarations, and alpha tests are retained.
The constant is 128/255, not an approximate 0.5. Generated GXP bytes are embedded.
Existing baseline programs remain available for fallback.

## Runtime boundaries

- Only owned, single-level decoded RGBA uploads at most 4×4 qualify; every actual
  texel must equal `0xff808080`. Row padding is excluded from the proof.
- The upload is pinned using the existing immutable draw-snapshot mechanism.
  Later guest writes allocate a new upload rather than changing recorded pixels.
- Cube/compressed uploads, copied/foreign descriptors, render targets, previous
  frame substitutions, border/unknown addressing, and unknown materials do not qualify.
- Shader overrides disable specialization. Failure to load a specialized shader
  falls back while preserving the selected alpha mode where available.
- `XV_RENDER_PROFILE=1` reports `[neutral-detail]` draw/index counts for shaders
  actually selected, including zero counts when no uploads qualify. Counters
  reset each reporting window; fallback shaders are not counted.

## Validation

- `make -C recomp/host test-neutral-detail`: source transformation and embedded
  bytes for all 18 variants; upload proof, all texel/component bits, padded rows,
  immutable previous uploads; recording eligibility with default-off/override;
  shader-link reuse and load-failure fallback for all three alpha modes.
- Render-profile tests pass, including inactive calls and window reset.
- Texture-preparation tests pass in normal, disabled-cache, and override modes
  (54,976 recordings each). The separate ASan/UBSan texture-binding replay passes
  outside the sandbox; its sandbox run encountered the known LeakSanitizer ptrace
  restriction rather than a reported memory defect.
- Changed runtime files compile with the Vita SDK into an isolated build directory.
- Isolated Vita3K shader compiler completed 18 programs with zero compilation
  failures. Vita3K subsequently crashed during compiler-app shutdown. This was
  compilation validation, not a gameplay or visual comparison.

Artifacts and logs are under `local/ce-neutral-detail-20260918/` (ignored).

## Remaining validation

Run matched default/off/on Blood Gulch and campaign views, collecting profile
counts and screenshots. Confirm real uploads qualify, check visual equivalence,
and then compare frame times on Vita at the same clocks and resolution. Keep
other experiments fixed. Tiny detail maps may already be cache-resident, so the
bandwidth/time benefit can be small even when a sampler is removed. No emulator
or hardware FPS improvement, nor a fix for distant texture noise, is claimed.

## Subsequent emulator screenshot check

Built a complete isolated executable from the working tree and launched it in
Vita3K with `XV_NEUTRAL_DETAIL=1` and `XV_RENDER_PROFILE=1`. Blood Gulch loaded
and rendered. The stationary base view reported **zero specialized draws** in
consecutive windows, including mesh 7260–7439. This view therefore does not
demonstrate a performance benefit; determine which eligibility condition rejects
its detail uploads before presenting this optimization as useful in gameplay.

Capture: `local/ce-neutral-detail-20260918/blood-gulch-experimental.png`.
The image is a direct game-window capture with black margins cropped. It is
not an off/on comparison. Texture noise remains visible. The isolated executable,
profile, and logs are preserved beside the capture; tester/device files were not
changed.
