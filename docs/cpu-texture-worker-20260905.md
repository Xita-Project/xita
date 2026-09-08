# September 5 texture worker and combined hardware candidate

The user requested CPU changes together with the successful local rendering
fixes on the Vita. Large texture conversions now split between a persistent
core-0 worker and the guest caller. Physics and AI retain their existing ordered
execution. This is a first bounded workload-sharing change, not evidence that
the hardware now sustains 20 fps.

## Work and ownership

`XV_TEXTURE_WORKER` defaults on; setting it to 0 selects the serial path.
Only aligned, power-of-two images at least 32 pixels in each dimension and
16,384 pixels overall are eligible. The caller converts the first half while
the worker converts the second. Ranges partition source texels for swizzled
formats, output rows for linear formats, and block rows for DXT. Small images
and cache hits avoid thread wake/wait overhead.

One fixed job slot and two semaphores bound the work. The descriptor captures
the source, destination, palette, dimensions, pitch and diagnostic options.
The guest cannot resume or modify these inputs until the call joins; no guest
fiber switch occurs inside conversion. Output regions are disjoint, with cache
line boundaries preserved by the eligibility restrictions. The worker cleans
its writes through the existing kernel cache hook before publishing completion.
It never accesses the caller's shared flush queue, texture cache or GXM API.
Coverage adjustment, mip generation and texture-cache publication follow the
join on the caller. Creation failures fall back to serial conversion, and
shutdown joins before releasing synchronization handles.

`[cpu-work]` reports window totals for jobs, pixels, caller conversion time,
worker time and join time. Caller and worker times overlap and must not be added
to frame time. Selected-mip decoding is already inexpensive during many gameplay
windows, especially with 64-pixel textures, so a major steady-state FPS gain is
not expected from this change alone. The emulator's requested affinity does not
substitute for measurements on the Vita.

## Validation and packaging

- 864 format/size/palette cases exercise the actual worker with pthread-backed
  Vita semaphores, checking identical output, bounded writes, buffer lifetime,
  one persistent worker, startup failures, disable/re-enable and clean shutdown.
- The same test passes AddressSanitizer and UndefinedBehaviorSanitizer.
- 432 conversions are byte-identical to the decoder saved before this change,
  including pitched rows and partial DXT blocks.
- Native build, shader loader/cache, texture cache, frame handoff/completion,
  frame pacing, CPU-reporting and render-target lifecycle checks pass.
- The exact padded SELF boots in Vita3K. Worker logs confirm execution and
  separate caller/worker/join timing; no hardware FPS improvement is claimed.

All 529 table-referenced fragment GXPs are also embedded in the executable
(911,912 bytes of program data). Loader tests cover every embedded program with
and without the explicit device override. This keeps executable-only USB updates
complete despite the card's previously observed allocation overlap. Old device
shader files cannot override the bundled programs unless explicitly requested.
The final emulator check uses the Vita's existing shader files and configuration.

The combined build retains the frame-index lifetime fix, sky/combiner corrections,
decals and active-camo scene-copy correction. The 20 fps cap remains a local test
setting. Angle-dependent rock/Warthog disappearance is still unresolved. Campaign
map files a10, a30, a50, b30, b40, c10, c20, c40, d20 and d40 were verified present
on the Vita; the a10-to-a30 transition has not been validated on hardware.

Candidate, backups and validation logs:
`/home/birchwoodgod/xita-backups/2026-09-05-201909-cpu-usb/`.
See its deployment record for transfer status and executable hashes.

USB transfer completed at 20:26 CDT. Direct reads and a fresh remount verified
the installed executable; the card was safely unmounted at 20:28. All 648 other
backed-up files, including settings and saves, were unchanged. Installed SHA-256:
`a4e44156923357bc3e8ff91725a870456b66102d8bac3c9f5084c60057e29bd1`.
Hardware startup is confirmed. The [follow-up report](hardware-20260905-cpu-worker-followup.md)
contains the collected frame/core profile and remaining issues.
