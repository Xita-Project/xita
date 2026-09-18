# Visibility dispatch and geometry lifetime follow-up

The new visibility audit identifies which algorithm each owned Xbox map uses.
Blood Gulch selects subcluster bounds (`52E10`), rather than the per-triangle
outcode loop (`537E0`). Optimizing the latter cannot explain or improve the
reported valley performance under this map's original dispatch state.

## What runs in the installed build

The captured-vertex build remains installed, runtime SHA-256
`f4fc1cd90db565ebd447f4a9acfe604b96f0e6bf7ab9162027da880eae697ae0`.
This follow-up does not change the executable or graphics settings. The user's
12 FPS valley observation predates that capture update and is not its measured
speedup.

One later ordinary-play window at camera `(45.74, -95.93, 2.15)`, direction
`(.59, -.81, .01)`, reports 10.6 FPS and 188 draws per frame. Across 60 frames,
the capture worker completed 9,840 jobs with zero failures. Owner copying took
5.13 ms/frame, worker preparation 6.80 ms/frame, and the owner completion wait
0.055 ms/frame. These overlap; they must not be added into a frame budget or
treated as a controlled before/after comparison.

The same window's first scene-detail interval is 20.51 ms/frame and includes
the call to `539C0`; model preparation occupies a separate 14.97 ms interval.
The visibility interval includes setup and traversal, so it is not the self
time of any particular math helper. The log records 4,680 clipping regions,
39,240 polygon clips and 356,520 input vertices across those 60 frames. These
counts are not triangle counts or GPU draws.

## Dispatch audit

`tools/audit_visibility_dispatch.py` checks the complete supported XBE identity,
two function signatures and six independently decoded child call sites. In
`539C0`, the first cluster's subcluster count at `+0x34` selects `52E10` when
nonzero and `537E0` otherwise. The former calls `5C300`; the latter calls
the four-plane vertex outcode function `12420`.

All 24 owned maps and 82 BSPs pass bounded structural inspection. Eight malformed
cluster, subcluster and surface-list cases are rejected. **80 BSPs select the
subcluster path.** Only `ui` BSP 0 and `d40` BSP 9 select the triangle path.
Blood Gulch has 30 clusters and 10 subclusters in its first cluster; Battle
Creek has 29 clusters and 28 in its first cluster. These are on-disk dispatch
inputs, not measured per-frame calls or a proof against runtime mutation.

## Preserved outcode prototype

`tools/visibility_outcode.py` accepts only one pinned retained `12420` body.
It keeps two temporary x87 values in native locals, preserving the original
guest reads, pushed ESI word, comparisons, branches and final context. Its
selector defaults off; there is no production build integration or deployment.

`tools/test_visibility_outcode.py` compares it with the actual retained Vita ARM
object, and checks the disabled lane too. All 484 cases match complete context,
guest memory, page tables and FPSCR without normalizing NaN payloads. Cases
cover all 16 output masks, all x87 TOP values, rounding/flush/default-NaN modes,
split pages, unaligned input, and stack/input aliases. An ordinary case executes
645 instead of 801 modeled instructions. This is not measured Vita CPU time or
an FPS gain, and its map dispatch makes it a low priority for the valley.

## Resource ownership findings

Independent decoding of the owned executable verifies model load completion at
`35463`, root publication at `35470`/`35478`, and resource registration at
`3547E -> 33930`. The vertex array has 12-byte resource records with Common=1;
the index records receive Common=0x10001 without the same Register call.
Disposal reaches `338D0` from `58471`, before clearing the model/tag roots.

Four unique direct vertex Lock calls found in reachable generated code are
confirmed in the XBE: `6649A`, `7A69D`, `7B15F` and `11623B`. These include
fresh initialized buffers and dynamic pools. The dynamic pool can retain a
writable pointer after Lock. This is not an exhaustive writer proof: direct
guest stores and retained aliases still prevent treating all model data as
immutable merely because later Lock calls are absent.

## Next work

Focus on portal clipping and the subcluster traversal selected by gameplay
maps. The original `structures_use_pvs_for_vs` flag exists, but enabling it
still runs portal traversal before copying precomputed visibility. Skipping
that traversal requires identifying every output consumer and measuring the
extra drawing caused by less restrictive visibility. It is not yet a qualified
shortcut.

The older native bounds helper is present but disabled in this gameplay log.
Its [September 8 trial](hardware-20260908-native-bounds.md) showed no established
gain under older settings. Do not label that historical result a definitive
failure of all native bounds work, or silently count the helper as enabled in
the current cumulative build.

Private evidence is under `direct-cluster-query/geometry-writers-20260917` and
`direct-cluster-query/visibility-outcode-20260917`. Owned executable/map bytes,
lifted bodies and ARM fixtures stay outside Git. Stable 20 FPS remains the goal.
