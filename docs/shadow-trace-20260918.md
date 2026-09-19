# Campaign shadow trace

The repeated floor silhouettes reported on September 18 are not yet fixed.
The reported emulator capture came from a different local build; it does not
by itself establish the same defect in the current hardware executable.

An archived campaign trace identifies a useful producer/receiver pair:
`halo_vs_39` draws models into 128 by 128 RGB565 render targets, and
`halo_vs_29` samples an offscreen texture while drawing receiving geometry
with blending enabled and depth writes disabled. Its raw pixel definition
`E5C340B4` resolves to canonical key `18CCED17` and the current
`ps_28CA2DAF_0D` fragment program. Both vertex programs write texture X/Y but
not W. This is evidence against blindly adding a texture perspective divide.
The archive also contains full-size 128 by 128 viewports and one 64 by 64
viewport; the latter still needs its target and draw identified.

## One-frame hardware capture

The authenticated `trace-draw` request now additionally records:

- Command, render target, shader identity, blending, depth and alpha state.
- Guest sampler addressing/filtering and the actual captured GXM texture
  dimensions, type, addressing and storage identity.
- Registers 0–3, 15–19 and 28–31 for commands that write an offscreen target
  or sample its storage, to inspect the shadow projection.
- Guest viewport changes during the selected frame.

Only valid captured texture descriptors are inspected. Unused bound stages
are labelled uncaptured; NULL storage cannot establish an offscreen alias.
This does not read back vertex/texture pixels, disable packed geometry,
change shaders or change submission order. These are recording-time values,
not proof that a later driver submission succeeded. Capture-frame timings
are diagnostic and must not be used for FPS comparisons.

The production logger and frame selectors pass normal and ASan/UBSan host
checks. The logger tests cover the inactive path without descriptor access,
unused bindings, effective sampler values, target producers/consumers and
NULL storage. The Vita build also succeeds.

## Performance context

Before deploying the diagnostic, ordinary campaign play on
`0.2.0-perf.2 / 13cb275` recorded stationary windows around 12.6–12.9 FPS
with 141–156 draws/frame, then different combat scenes at 5.8 and 5.4 FPS
with 311 and 343 draws/frame. This is not a matched before/after result.
Both object worker lanes executed jobs. In the last window, joined object
batches took 3,027,729 microseconds over 60 displayed frames (50.46 ms/frame),
and reported draw HLE time was 39.4 ms/frame. Timers overlap/inherit waits;
these numbers cannot be added into a CPU self-time breakdown. The hierarchy
copy change has not established an FPS gain, and stable 20 FPS heavy gameplay
remains unmet.

Private build, capture and test artifacts are under
`2026-09-18-unified-games/shadow-followup/` in the local backup root.

## Deployment

`0.2.0-diag.1 / 9c73da8` is boot-confirmed on the physical Vita in slot 1.
Runtime SHA-256 is
`c06613d76102d9a8246517a1a38e9d6eff330d4f1c828c48eb715be2b4ebcdfb`.
Package comparison with perf.2 changes only `game-a.self` and `boot-game.txt`:
shader assets, bundled Halo 2 and shared updater contract are identical.
The perf.2 executable remains in slot 0 for rollback.

The existing render-target lifecycle test initially failed to compile because
its source extraction also picked up an unrelated vertex-capture callback
added earlier. Limiting extraction to the RTT and UI recorder restores the
intended test scope without changing production code or suppressing warnings.
All six queued/synchronous scene-capacity configurations and the drop-target
configuration pass. These mocked checks verify ordering and ownership, not
hardware shadow pixels.

The same last perf.2 combat window attributes draw preparation to streams
17.312 ms/frame, textures 7.122, indices 6.914, state 3.501, program 3.022,
setup 1.592, constants 0.796 and diagnostics 0.274. These are caller elapsed
measurements. Vertex capture reports 937,666 microseconds on the recording
side and only 508 microseconds joining over 60 frames; its 296,086 worker
microseconds overlap other work. Existing reuse already avoided 45,114 KiB
of staging writes over that window (6,645 exact hits). The next CPU candidate
should target remaining stream capture/validation work, retaining those
working reuse paths, rather than assuming another worker removes the cost.

## First hardware capture and asynchronous-path correction

The first remote trace on diag.1 starts and completes exactly one frame
(10894), but emits no new draw-state records. The existing range/viewport
records establish that the request itself worked. Hardware draws use the
asynchronous vertex-capture branch, which returned before the new logger.
The initial host fixture exercised the logger directly and did not catch
that integration mistake.

Commit `cb1e0c6` moves the logger immediately after material/descriptor capture
and before the asynchronous return. It still does not join workers or read
unresolved vertex streams. The test now checks this ordering; normal and
sanitized logger tests pass. `0.2.0-diag.2 / cb1e0c6` is boot-confirmed in slot
0, executable SHA-256
`5fc8c5f2cb93a40cbc30634241a10a05e6c9295051f605b57e2c500c3abea33f`.
Diag.1 is the previous slot; the perf.2 package remains archived locally.
Only the executable and its boot hash differ from perf.2's package.
