# Original constant texture coordinates

Native199 stops at END `03B809E4` of an original fullscreen clockwise quad. All four UV pairs are positive zero and all four packed colors are opaque black. Texture0 at `018FAF80` is a fully captured 4×4 BC2 allocation (16 compressed bytes) whose decoded texels are uniformly opaque white. The program, constant bank and pipeline match the preceding packed-color rectangles, except for the permitted texture offset and dimensions. This is an original opaque-black overlay; it must execute rather than be skipped to expose the image underneath.

The H2-only sprite consumer now accepts either the existing complete normalized UV corners or four positive-zero pairs. These are whole-primitive alternatives: mixed corner/zero patterns, arbitrary coordinates and varying packed colors still reject. The real original shader samples texture0, multiplies the sampled color and blends into completed independent GXM staging. No replacement solid-fill path, alpha change or extra presentation is introduced. All existing resource bounds, physical/host alias rejection, exact program/state checks and RGB-only commit remain in force.

Two isolated Vita GPU probe targets reuse the same original compiled shader and constants:

```sh
make -C games/halo2_5849 -j4 sprite-constant-probe sprite-constant-source-probe \
  BUILD=/private/new-probe-build SPRITE_SHADERS=/private/owned-sprite-assets
python3 games/halo2_5849/check_sprite_probe.py --constant-uv \
  --prepared /private/owned-sprite-assets --results /private/probe-results
```

The extra private asset `sprite.constant.texture.bin` contains the complete owned 16-byte BC2 level, without its diagnostic header. The source probe uses the same checker with `--source-probe`. Existing sixteen-fixture probe targets keep their original cases.

Four full-target fixtures use zero UVs with opaque white, the original opaque black, partial-alpha independent color channels, and zero alpha. Each probe captures 1,228,800 pixels. All four unblended source outputs are byte-exact. Full blending is byte-exact for white, black and zero alpha; partial-alpha RGB differs from the independent equation by at most one level. Every destination alpha byte is preserved, and zero alpha changes no destination bytes. These are isolated shader/sampling/blending measurements, not a displayed menu or NV2A hardware equivalence claim.

All 54 host executables, the sprite ASan/UBSan suite and 54 shader Python checks pass. Host coverage includes all fourteen intermediate mixtures of the four nonzero UV corner words, negative zero and arbitrary/nonfinite coordinates, normalized color lanes, complete input/output ownership, failed staging and RGB-only commits for the four constant-UV cases. The native200 build verifies 180 dependency targets and 128 unchanged generated code units plus the two generated support sources. Among all 179 existing objects, only `sprite_draw.o` differs from native199. The shared renderer, audio, original game code and CE paths remain unchanged.

Private probe evidence is under `constant-uv/probe01-normal` and `constant-uv/probe02-source`; build evidence is in `constant-uv`. Native200 completes the original ninth rectangle, logged at END source `03B809E8`, then consumes GET=PUT `03B80B44` and advances the original vblank callback from game_count135 to136. It presents the next original frame, which is still entirely black. The original Microsoft Game Studios intro is visually confirmed, but **the original main menu is not visible**. The next strict stop is DSOUND entry `37B844`, called from return `21EC3C`, with saved caller EBP `005E6010` on the stack (the original wrapper takes no arguments). The next task is to audit that original sound work wrapper and caller; no result is invented to bypass it.

Native artifacts and captures are frozen in `native-200-artifacts`, `native-200-view` and `native-milestone-200.json`. The owned :111 emulator PID3542552 was stopped after capture. ELF SHA-256 is `795bc3a58a4d64fe3b7633f5f14956945ca6ba1ab1cd1c3620a544dfbd55e143`; EBOOT is `9b87487a6fc84595acd78cb21a557ef6931f2f2312161c60d5635a892b2fda3d`. Trace SHA-256 is `c2aa8320d3580cb86364eadf1c8b18bb9e5fd9d0f71f33c54694f0acf370e24e`, channel snapshot `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e`, and full black last-presented frame136 `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54`.

Exact private replay with the owned emulator stopped:

```sh
python3 preserve_fresh_cache.py native200-replay
python3 capture_run.py 200-replay native-200-artifacts
python3 drive_startup.py 200-replay native-200-artifacts
```

The diagnostic packages embed owned game code/image or shader assets and must not be uploaded as distributable releases.
