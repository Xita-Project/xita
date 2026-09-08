# September 5: Blood Gulch crash and frame handoff correction

The user reports poor performance and a crash in the newly deployed `caedaae7`
build. USB collection preserved the executable, current settings and log, all
53 save/cache files, and the newest crash dump before any update.
Evidence is under
`/home/birchwoodgod/xita-backups/2026-09-05-181134-bloodgulch-crash/`.

## Hardware evidence

Settings changed in the user's dashboard to 64-pixel textures, 848x480, linear
filtering and mip smoothing on. Nine sixty-frame gameplay windows total about
76.1 seconds and aggregate 7.1 fps, ranging from 4.9 to 9.5 fps. These are rounded
log timings from a failing run, not a controlled comparison with earlier builds.

There are 330 failed render-target BeginScene calls. The first uses target 90;
later targets include 166, 99 and 245, although the pool contains only eight slots.
Errors include `805B0004` (invalid pointer), `805B0017` (driver) and `805B0003`
(invalid value), as defined by the local VitaSDK GXM header.
The last logged CPU sample is at 183.2 seconds after launch.

The newest dump is
`data/psp2core-1788649792-0x00089721bf-eboot.bin.psp2dmp.tmp`.
Its gzip stream decodes successfully, but some ELF program headers extend beyond
the available contents. Readable thread notes place `xv_pump` at PC `E0092924`;
the guest Present fiber is waiting. An older, unrelated GPU dump returns a USB
read error and was not included in the successful current-file manifest.

## Correction

The two-buffer publication sequence reset the next mesh/UI ring entries before
waiting for the preceding frame's renderer. With a slow pump, those entries still
belonged to its active frame. Resetting `ncmds` after replay passed index zero
meant its `i == ncmds` exit condition could never be reached, so replay continued
into unrelated memory and treated arbitrary bytes as target indices. Publishing
the new UI index early could also redirect an older frame's UI replay.

Present now waits for the preceding published frame before rotating/resetting
either ring. The pump completes GPU work before releasing its frame fence, so
frame-owned vertex/index/UI scratch is safe to reuse. Recording the next frame
still overlaps rendering of the current one. The render-target walker also uses
an inclusive end check and rejects invalid or missing targets before indexing
the pool, closing an open scene on that failure path.

The real publication-path host test fails against the old ordering and passes
120 frames with a deliberately unfinished consumer after the correction. It also
passes address/undefined-behavior sanitizers. A separate test runs the production
pump against a GPU mock that retains scratch until Finish. Render-target tests
cover invalid targets before and after opening a scene, plus invalid UI targets.
Native build, shader/HUD/cache tests and texture tests pass.

This establishes a frame-lifetime defect and its correction; hardware retesting
is still needed to confirm the reported crash is gone. It does not establish a
fix for every geometry spike, sky haze, flashlight issue or missing projectile.
The user revised the first performance target to stable 20 fps, with 25 fps next.

## Recovery artifact

`recovery/eboot-usb.bin` is compressed and padded to the existing 32,918,474-byte
allocation. Its three decompressed SELF segments match the native build.
SHA-256: `2c55247bcdd5846f8d7665d2912bc2521c865864ecfdf3bf1adf06929767fa20`.

The native executable ran Blood Gulch for eight minutes in Vita3K, including
the user's weapon/effect testing, with no failed/invalid render-target or full
combiner-cache messages. Its screenshots still show rendering defects. A separate
Vita3K instance booted the exact padded candidate and reached Blood Gulch too;
the original gameplay session was preserved. Emulator logs contain host SIGSEGV
diagnostics while gameplay continues; this is not a clean emulator diagnostic
log and does not establish hardware stability or frame rate.

The executable was deployed over USB at 18:31 CDT by exact-length in-place
overwrite and fsync. Direct reads verified its hash before and after remounting
read-only. All 938 other files in the current manifest retained their size and
hash, including the user's latest 64-pixel texture setting, all 53 save/cache
files and the new crash dump. No new files were allocated on the Vita. The volume
was safely unmounted at 18:32 CDT. See `recovery/deployment.json` for the complete
transfer record. Hardware launch, crash retesting and sustained 20 fps remain
unverified; plasma projectiles, view-dependent sky haze and purple camo are open.
