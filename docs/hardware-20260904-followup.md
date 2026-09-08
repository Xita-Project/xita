# September 4 hardware follow-up

The installed `xita-20260904-roadmap-test.vpk` reproduced the user's report:
4–5 fps in a10, missing campaign resume, geometry spikes and blocky baked lighting.
The sustained 25 fps requirement remains unmet. Emulator frame rates are not hardware
performance evidence.

September 5 update: the user reports reaching **22 fps in Blood Gulch** and
performance improvements from shader fixes. The exact build, scene and duration
are not recorded. That observed rate corresponds to 45.5 ms per frame, about
5.5 ms above the 25 fps budget. It supports investigating shader/rendering costs
alongside guest execution, while a10's much slower scenes need separate profiles.

## Changes

- **Original loading artwork.** XDK 3925's `SetVertexData4f(-1, ...)` completes a
  pretransformed vertex; it was being treated as attribute 15 and dropped. Capture
  the complete attributes, retain vertices until the frame fence, and render the
  game's original two register-combiner programs. Linear texture coordinates are
  normalized per stage. Constant updates invalidate the cached uniform snapshot.
  The blur samples the completed display surface, since the guest backbuffer RAM
  contains no rendered pixels. A GPU finish is required only on these feedback
  frames. Startup and map-load artwork were observed in an isolated Vita3K setup.
- **Baked-lighting filters.** Forward the game's sampler settings: XDK 3925 table
  entries 10/11 are U/V addressing and 13/14 are magnification/minification. The
  bridge had left its point-filter defaults active even when Halo requested linear
  filtering. This preserves the existing lightmaps and does not synthesize shadows
  or add mip levels. Hardware appearance still needs comparison.
- **CPU memory helpers.** Keep fixed-size, single-page SSE/x87/string accesses
  inline and move fragmented-page copies to a slow helper. ARM assembly inspection
  confirms ordinary loads/stores replace generic `memcpy` calls on the fast path.
  Page-crossing and overlapping-copy semantics remain covered by host regressions.
  The hardware speedup has not been measured.
- **File I/O.** Read and write guest buffers across separately allocated pages.
  Adjacent host pages stay in one I/O request, preserving large map-streaming reads.
  Previously, a single `X_G` translation incorrectly assumed the entire buffer was
  contiguous. EOF, short I/O, partial errors and completion reporting are tested.
  Creation now requires an existing parent directory. In Vita3K, XAPI's
  `OPEN_ALWAYS` metadata probe had created an empty `SaveMeta.xbx` and its missing
  parent, causing the subsequent new-profile operation to fail. With the parent
  check, a clean profile is created, signed, and opens again after restarting.
- **Save signing.** Recompile the original XAPI Begin/Update/End functions instead
  of returning `INVALID_HANDLE_VALUE` immediately. Implement the SHA-1 kernel
  exports and title signature-key data export, using the loaded XBE certificate.
  Contexts remain in guest memory, including across fragmented pages. The virtual
  console uses a fixed zero HD key for non-roamable signatures; this does not import
  a physical Xbox's console-bound saves. A fresh-profile save/quit/restart test
  still fails to offer checkpoint resume; signing alone does not fix that path.
  September 5: this experiment is now opt-in through the recompiler's `--lift`
  option. Regular and profiling builds retain the existing unsigned-profile
  behavior until migration and checkpoint validation are fixed.
- **Profiling.** Make the 1 kHz sampling thread opt-in via `XV_PROF=1` in normal builds.
  Include zero-address samples as `unattributed` rather than omitting most guest CPU
  time from the table. Use a `--trace-funcs` recompile for function attribution and
  measure instrumentation overhead separately from the normal build.
  September 5: `--trace-funcs` builds now start sampling automatically, unless
  explicitly disabled by `XV_PROF=0`; their log states whether guest tracing is present.
- **App icon.** Replace the 128×128 indexed PNG with the generated green X design.
  The full source and exact generation/edit prompts are in `assets/branding/`.
  The normal development VPK includes it automatically.

The loading vertex buffers add 128 KiB of uncached GPU memory (two 256-vertex
buffers with 256-byte records). Previous-frame feedback uses existing display
buffers; there is no additional framebuffer allocation. SHA contexts are 116 bytes
each in the game's existing allocation; two key exports use two 64-byte kernel-pool
slots. The earlier kernel-pool review fix adds 96 KiB of allocator metadata.

The loading fragment definitions were captured from the user's own executable's
runtime state (`C4B1822B`, `C61481BC`) and translated through `tools/ps_pipeline.py`.
No loading bitmap or game data is added to the repository. Synthetic VS identity
`FFFFFFFE` supplies output mask `7F` for those pairs.

## Evidence and remaining work

The hardware log includes 217.2 ms of game work and 0 ms waiting per frame (4.6 fps),
with 22.3 ms in draw HLE and 5.5 ms in the render pump. Another sample reports
178 ms game work and 65.1 ms in the pump. These are overlapping pipeline timings,
not additive phases. CPU cost dominates the slow samples, while pump/decode cost
also varies. The previous sampling build did not instrument guest function entries,
so it cannot identify the dominant guest functions.

The hardware profile save has a zeroed header; the cache save still has its header.
Testing the cache copy alone did not establish a successful restore. The new signing
implementation is a fix for a definite missing dependency, not proof that the old
checkpoint can be recovered. Original Vita saves remain unchanged. Backups of logs,
screenshots and save copies are kept at
`/home/birchwoodgod/xita-backups/2026-09-04-hardware/` on the development machine.

An A/B test with the same copied user profile reaches **Load Level** in the
pre-signing build and reports **Unable to load saved game file** with signing
enabled. The old `blam.sav` signature at offset 48 is zero. A freshly created
profile has the expected HMAC-SHA1 of its first 48 bytes there and opens correctly
after an app restart. Legacy profile compatibility therefore remains a deployment
blocker for the combined development build; original user data has not been migrated.

The fresh profile reached the a10 cryo tutorial and completed **Save and Quit**.
The log records a full 3,428,352-byte checkpoint write, then a 332-byte header write,
then another 16,384-byte write that zeroes the header. Restarting again offers
**Load Level**, not **Resume**. This reproduces the header loss even with working
signatures. Isolated logs are `/tmp/xita-save-parent.log` and
`/tmp/xita-signed-profile-restart.log`; the saved test data is in
`/tmp/xita-loading-test/ux0/data/xita/save/`.

Geometry spikes remain open. W-clamping is still disabled by default; changing it
warps otherwise normal geometry. Packaged shader precedence prevents stale local
overrides from silently replacing the current programs, but that does not prove
the near-plane issue is fixed.

The `dsound-state`, `offscreen-rt` and `adhoc-net` branches were inspected. They are
not merged into this candidate. Sound playback reporting is relevant to campaign
timing but needs its own integration check; offscreen targets add their own memory
and synchronization costs; networking follows the performance gate.

## Validation

Run `make -C recomp/host test test-shaders` for memory, allocator, immediate vertex,
sampler, file I/O, SHA, profiler and shader-loader regressions. Memory, immediate,
file I/O and SHA tests also pass AddressSanitizer/UndefinedBehaviorSanitizer.
SHA tests include published empty/`abc`/million-`a` vectors, incremental updates,
padding boundaries and fragmented context/input/output. The recompiled Halo 3925
XAPI signing functions were separately exercised against Python HMAC-SHA1 for both
signature modes, including register/stack preservation and allocation failure.
With locally recompiled Halo 3925 sources generated using `--lift
XCalculateSignatureBegin XCalculateSignatureUpdate XCalculateSignatureEnd`,
reproduce that experimental integration check using `make -C recomp/host test-signing`.

Once the profile compatibility and checkpoint failures are resolved, install the
combined candidate with VitaShell, then check startup/map loading artwork,
nearby wall geometry, baked shadow edges and a new save/quit/relaunch/resume cycle.
Performance acceptance uses the five-minute routes in [ROADMAP.md](../ROADMAP.md),
with loading reported separately. No 25 fps result is claimed for this candidate.

An independent **icon-only** package, `xita-20260904-icon.vpk`, is staged at the
Vita card root. It is based on the installed `xita-20260904-roadmap-test.vpk`;
every archived file except `sce_sys/icon0.png` is byte-identical. It includes no
runtime fixes and does not install itself or modify saves. The combined development
`xita.vpk` remains local while the save regressions are investigated.

See [September 5 hardware follow-up](hardware-20260905-followup.md) for the next
test: the installed runtime was confirmed to lack the loading fix, and signing
was returned to its earlier behavior so the loading/profiling build can be tested
with existing profiles.

Format references: [Cxbx sampler-table conversion](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/D3D8/Direct3D9/TextureStates.cpp),
[SHA context ABI](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/kernel/exports/EmuKrnlXc.cpp),
[title-key derivation](https://github.com/PMStanley/xbox-save-sig/blob/master/xboxsig.py).
