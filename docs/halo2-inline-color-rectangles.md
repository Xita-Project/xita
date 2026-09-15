# Original packed-color rectangles

The opt-in `SPRITE_RENDER=1` consumer connects the [native196 GPU proof](halo2-packed-color-probe.md) to original inline commands. It requires `LUMA_RENDER=1`. Owned shader programs, reference data and diagnostic packages remain private; the package embeds owned game image/code and must not be uploaded as a distributable release.

Admission is deliberately bounded: original `BEGIN QUADS` (8), exactly twenty non-increasing `INLINE_ARRAY` (0x1818) words and `END`. Each of four vertices is float2 position, float2 UV and normalized little-endian BGRA color. The supported geometry is an axis-aligned clockwise rectangle in coordinate range −2048..2048, full normalized UV corners (or the later validated all-zero UV pairs) and equal packed colors at all four vertices. It uses the exact owned 21-slot program, constant bank, setup validity and pipeline from native196. Other geometry, varying colors, depth/stencil, alpha tests, texture routes or combiner states reject.

Texture0 is a single-level power-of-two BC2 image, at most 1024 per axis and 8192 compressed bytes. A separate rendering view reader validates its complete DMA read span; existing exact 8×8 readers and the diagnostic snapshot reader keep their prior restrictions. Texture stages whose values cannot reach the final output and disabled depth do not authorize memory access. Source and destination must have disjoint physical and returned host spans. The full 640×480 ARGB destination requires both read and write permissions because blending reads its previous RGB.

The existing H2 GXM context receives private compressed source and destination snapshots. The rectangular BC2 block-order conversion preserves compressed bytes. A completed full-target copy seeds independent GPU staging, then the original vertices and uniforms are restored. The validated shader runs with linear filtering, clamp addressing, original clockwise-front back-face culling and source-alpha/inverse-source-alpha blending. At successful END, the consumer commits RGB only, preserving every guest destination alpha byte. The original flip still controls presentation; no menu or alternate screen is drawn by the adapter.

All active consumers retain exclusive ownership of subsequent commands, including rejection. The ninth-consumer dispatcher tests check this and preserve the caller's floating-point control state. Host tests also cover normalization, malformed/extra/incomplete packets, nonfinite and invalid geometry, all permitted texture dimensions, state/program/constant changes, DMA and returned-pointer bounds/aliases, failed staging, completed-count overflow, and RGB-only commit. The owned queued suffix independently contains six matching rectangle/state contracts; that static audit alone is not execution evidence.

Validation: all 54 host executables, sprite/texture ASan+UBSan suites and 54 shader Python tests pass. The default-off GXM object is byte-identical to native196. Its dispatcher object differs only in the order of independent `LDR fp,[sp,#36]` and `MOV r8,sl` instructions; section size and all other sections are unchanged. No CE source or emulator configuration changes are involved.

Native197 executes all six original rectangles after the game completes its 59,670,016-byte main-menu map copy and receives an ordinary eight-second Start press. END sources are `03B7F0A8`, `03B7F3CC`, `03B7F6F0`, `03B7FA14`, `03B7FD38` and `03B8005C`. The first four diagnostic comparisons report no RGB change, consistent with the original alpha value 1/255. The consumed batch finishes at GET=PUT `03B80158`. The next strict stop is an unresolved original callback `22D82C` from `22D2EE`, return `22D44E`, before another presentation.

The intro screenshot is visually confirmed. The terminal screenshot and complete last-presented frame135 remain black; **the original main menu is still absent**. The six completed draws are offscreen execution evidence, not a menu or general shader-compatibility claim. The owned :111 emulator process was stopped after the terminal capture.

Private evidence is under `private/native-197-artifacts`, `private/native-197-view`, `private/native-milestone-197.json` and `private/sprite-consumer`. All 180 dependency targets were verified after the four-job build. ELF SHA-256 is `b3348127d0bfd62b57a210d6934a8377119df2da270080336e8efe10fa12e9c9`; EBOOT is `9ec6e01b9f8c7a754ec0fb77e6445f836319dc160934b37541eeed54529da0d4`. Trace SHA-256 is `9684ebbb692baea75c8bb0b53c69e0206dd3ba06c2009ee5f2fa2c066bf735f3`, channel snapshot `db5d4f757594407c3ce2a02fe64717b5326646a5efca143a9b871307d552fd33`, and last-presented image `20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893`.

Reproduce this exact package from the private directory with the owned H2 emulator stopped:

```sh
python3 preserve_fresh_cache.py native197-replay
python3 capture_run.py 197-replay native-197-artifacts
python3 drive_startup.py 197-replay native-197-artifacts
```

The helper preserves the previous private cache; the original game performs the new cache copy. Do not reuse another running emulator's lab or copy generated artifacts into Git.

The subsequent all-zero UV route and native200 evidence are documented in [constant texture coordinates](halo2-constant-uv.md).
