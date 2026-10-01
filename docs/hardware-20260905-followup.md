# September 5 hardware follow-up

The user reported a blank loading screen and about three minutes playing a10.
The Vita was connected over USB and its logs and save files were copied to
`/home/birchwoodgod/xita-backups/2026-09-05-060154-hardware/` before any staging.
Original device saves and the installed app were not changed by this investigation.

## Installed build and loading screen

The installed `eboot.bin` is 29,093,842 bytes, SHA-256
`5e3ff0708d54272d30f1e30f46780c21400a8631029466b9c795f6b726f89044`.
It matches both `xita-20260904-roadmap-test.vpk` and the icon-only package.
The installed app has none of the three new loading programs:

- `shaders/xv_passthrough.gxp`
- `shaders/ps_C4B1822B_7F.frag.gxp`
- `shaders/ps_C61481BC_7F.frag.gxp`

This run therefore did not exercise the loading fix. The fix had only been
validated in Vita3K; the icon-only package preserved the earlier executable.

## Performance evidence

The latest `xita.log` contains 23 intervals with at least 10 BSP draws per frame.
Each interval covers 60 game Presents, together representing approximately
194 seconds. These intervals include transitions; this is not a measured steady
gameplay benchmark or an independent count of displayed frames.

- Logged interval rates: **4.3–9.6 fps**.
- Throughput across those intervals: approximately **7.1 fps**.
- Draw HLE elapsed time: **10.7–45.3 ms per frame**.
- Texture decode elapsed time: **4.8–19.3 ms per frame** after dividing the
  logged 60-frame decode totals by 60.
- The last three sampling tables name only about **19–21%** of samples. The old
  executable lacks guest-function instrumentation and omits the zero-address
  samples from its output, leaving most execution unattributed.

The largest named HLE slot, `0x1842D0`, is `D3DDevice_DrawIndexedVertices` in the
local symbol database. Game-side, render-pump and HLE timings overlap and must not
be summed. These logs do not isolate GPU shader execution time.

The separately reported **22 fps in Blood Gulch** remains recorded as a user
observation; this a10 run does not supersede it.

## Candidate scope

The next diagnostic build includes original startup/map loading artwork, sampler
forwarding for baked lighting, the new icon, and the memory-helper changes. It
uses `--trace-funcs`, with sampling enabled automatically so the next hardware run
can identify guest functions as well as draw HLE costs. Instrumentation adds work;
its FPS is diagnostic and must be compared with an ordinary build later.
Indirect calls now restore the caller's sampling marker on return, including
native HLE calls that invoke guest callbacks. This prevents subsequent caller
work from being charged to the last callback. The samples measure wall time;
they do not provide exclusive CPU-cycle or GPU-time measurements.

The original XAPI signing experiment is now explicitly opt-in via `--lift
XCalculateSignatureBegin XCalculateSignatureUpdate XCalculateSignatureEnd`.
The diagnostic build retains the earlier unsigned-profile behavior. This permits
testing with the user's existing profiles; it does not claim to fix checkpoint
resume. The experimental code and its tests remain available.

Host regressions cover normal/traced profiler defaults, explicit overrides,
sample accounting, nested/cached indirect call attribution, memory access,
texture sampler state, immediate vertices,
file I/O and file creation. Hardware loading appearance and the 25 fps target
remain unverified until the actual candidate is installed and run.

## Package and validation

`xita-20260905-loading-profile.vpk` is staged at the Vita storage root and in
the repository root, with a preserved copy and `manifest.json` under the backup's
`candidate/` directory. The device copy was read back and SHA-256 verified.

- Package: 13,205,446 bytes; SHA-256
  `d6b11dddef8e1f04038c60671e7ef44aaabbb7e2305242c507014a8b38322163`.
- New executable: 32,912,854 bytes; SHA-256
  `12d858bddf8390a63c2610b2082dc16b1fc7963e830c48b9c3b24e20e430e2aa`.
- All three loading shaders and the icon match the source-tree binaries.
- Build commands: `tools/recomp.sh haloce/default.xbe --trace-funcs`, then
  `make RECOMP=1 -j6`. The Makefile reserves VitaSDK's `__sce_headroom` for
  module/import metadata; the traced executable previously left only 144 bytes
  before its next segment, causing `vita-elf-create` to fail.
- `make -C recomp/host test test-shaders` passed. Indirect dispatch attribution
  also passed AddressSanitizer and UndefinedBehaviorSanitizer.
- Isolated Vita3K testing opened the copied **New001** hardware profile, showed
  startup and first-level loading animations, and reached the cryo-room tutorial. Sampling
  started automatically and produced both guest-function and HLE addresses.
  Screenshots and logs are preserved under the backup's `validation/` directory.

The emulator used `XV_SLOW_READ=2000` to make loading artwork easier to capture,
plus save/error logging. These settings were not copied to the Vita. Emulator
timings are not hardware performance results. Existing geometry defects and
checkpoint resume are still unresolved.

Next hardware test: leave USB mode, install
`ux0:xita-20260905-loading-profile.vpk` with VitaShell, launch Xita, check startup
and first-level loading artwork, then play a10 for 2–3 minutes and reconnect in
USB mode. No profiler configuration is needed. Installing the named VPK is a
required separate step; staging it alone does not update the installed app.
After staging, the installed executable hash and all 14 backed-up save files
were checked again and remained unchanged. The USB filesystem was unmounted
successfully after verification.

## Subsequent user validation and priorities

The user subsequently reported that the loading screen is beautiful and that
the baked lighting is fixed. Both visual fixes are therefore confirmed by the
user on Vita hardware. This does not establish an FPS gain or geometry fix.

Checkpoint testing is difficult while the game is slow and geometry spikes.
Defer that hardware request until the game is playable. Prioritize geometry
stability and the 25 fps target; preserve the existing profile compatibility.

A follow-up texture-cache correction addresses unnecessary re-decoding: the
initial hash used the selected mip, but subsequent checks used the texture's
level-0 address. Cache entries now retain the selected source address for both
hash comparison and streamed-file invalidation. Regression coverage exercises
unchanged and modified DXT1/3/5, RGBA, luminance, dynamic and cube textures.
The old check fails the unchanged-upload assertion; the corrected cache passes
normal and AddressSanitizer/UndefinedBehaviorSanitizer runs. This correction is
newer than the installed loading/profile package; hardware FPS impact is pending.

The follow-up package is `xita-20260905-texture-cache.vpk` (13,206,188 bytes),
SHA-256 `926cd2be9e6fd327be4d2513a4dfede71c6e46499c519588b146c4017578f153`.
Its executable SHA-256 is
`b42abccfd1da7266751ef62deb6f0641d0b23bd8d7727402241ccfb2d2b39784`.
It was initially built locally while USB storage was disconnected, then staged
after the [Keyes/combat hardware run](hardware-20260905-keyes.md). All packaged assets
other than the executable are byte-identical to the loading/profile package.
The isolated cryo tutorial reaches gameplay; comparable emulator intervals went
from 44–46 decodes per 60 Presents to mostly zero, with occasional real updates.
These runs are not identical input replays and do not establish a Vita FPS gain.
Evidence is preserved in `/home/birchwoodgod/xita-backups/2026-09-05-072254-texture-cache/`.

## Hardware-only geometry and flashlight reports

The user clarified that geometry spikes occur only on the Vita; emulator
rendering is good. Turning on the flashlight makes **walls and models disappear**,
not merely the spikes. Track these as distinct symptoms. The user also suspects
bloom/lighting; the responsible pass has not yet been identified.

The buffer audit found that the linked executable leaves `xv_kmod_dcache_clean`
as an unresolved weak symbol, so its cache-flush calls are no-ops, while the guest
arena uses `USER_RW` and draw commands retain guest-memory pointers across
asynchronous recording/replay. Cache visibility and buffer lifetime require
hardware investigation; this does not prove they cause either visual failure.
The upstream [Vita3K GXM implementation](https://github.com/Vita3K/Vita3K/blob/master/vita3k/modules/SceGxm/SceGxm.cpp)
also leaves W-clamp enable/value calls unimplemented, so emulator images cannot
validate that experiment. No geometry workaround was enabled in the cache fix.
