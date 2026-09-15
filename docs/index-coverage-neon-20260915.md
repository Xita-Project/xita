# Indexed coverage reduction — September 15

Indexed vertex validation reduced whole-frame time in the earlier physical
cryo-room comparison, but added index preparation cost: approximately 3.69 to
6.08 ms/frame in nearby profiling windows. Later user gameplay logs still show
about 5.4–5.8 ms/frame in index preparation. Texture preparation in those later
windows is about 2.5–2.6 ms/frame. These categories are elapsed owner time and
include preemption; they are not independently additive to the full frame.
See [the original measurements](vertex-references-20260908.md).

The existing `XV_DRAW_SCAN_NEON` option accelerated plain index bounding, but
indexed validation selected a different scalar coverage builder first. With
`XV_VERTEX_REFERENCES=1`, that NEON index-bound helper was bypassed. This change
adds an exact NEON reduction to the coverage builder and selects it through the
same existing draw-scan option. The option remains off by default.

For at least 256 indices, the new helper captures the same cached 256-index
chunks as the original helper. Each index sets its eight-vertex coverage bit
without a conditional unique-group update. NEON reduces the maximum index and
counts the occupied bitmap bits once after capture. Scalar tails still count
in the maximum; the initialized bitmap is bounded to 256 words. It has at most
8192 occupied bits, which cannot overflow the 16-bit partial accumulators.
Smaller draws and non-NEON builds retain the original helper.

The GPU receives the exact captured index sequence in its original order.
Source lifetime, index allocation, GPU publication, vertex snapshots and
referenced-record equality are unchanged. No vertex or triangle is removed.
`[index-coverage]` reports reference copies and large batches actually routed
through the compiled NEON path; counts reset with the existing frame profiler.

## Checks

The VitaSDK compiler defines NEON for the actual runtime compile flags. The
Vita-compiled helper passes 3566 capture checks against a separate Python oracle,
covering all 65536 index values, lone maxima in vector lanes and tails, every
bitmap-word boundary, unaligned sources/destinations and unmapped end guards.
Strict hooks check source/destination/bitmap access ranges and prohibit writes
to source data or read-only ELF segments. Exact masks, group counts, maximum
indices and output bytes agree with the original helper. Referenced/unreferenced
vertex-mutation checks also continue to pass.

The available Unicorn 2.1.4 environment failed this test's existing Thumb IT and
memory-hook self-check before candidate execution. That failed run is preserved.
A separate private environment pinned to Unicorn 2.1.3 passes the self-check and
all tests; the bounds hooks were retained. Copy/fill firmware calls are modeled,
so this does not test the real Vita memory bus, firmware timing or GPU caches.

For 256–4096 indices, repeated-high, sequential and random fixtures execute
17.7–42.8% fewer modeled ARM instructions. A 128-index fixture uses the old path
and adds five dispatch instructions. Firmware copy counts and byte totals are
identical. These are instruction counts, not cycles, milliseconds or FPS, and
are not a predicted percentage improvement to the full game.

Production retention/upload tests pass under ASan/UBSan, including 4000 draws
across 500 slot generations and borrowed/invalid index paths. Existing draw-state,
shader-cache and index-copy host checks pass against the current build inputs.
The native package builds with the same asset contract; only its executable
and boot selector change.

## Comparison

Use the authenticated remote `benchmark OUTPUT --kind draw-scan --runs 1`
command in a loaded, stationary view with indexed validation enabled. This is
the existing draw-scan off/on/off test: it also toggles the already implemented
constant comparison path. Consequently, its whole-frame result measures that
combined option, not this helper alone. Resolution, other graphics options,
object workers and vertex-preparation configuration remain fixed. Completion,
cancellation and lost-view paths restore the configured option.

Physical hardware remains on the prior confirmed runtime while its remote
service is unavailable. No hardware update, FPS gain or GPU-crash resolution
is claimed. The hardware comparison and representative driving/NPC gameplay
remain required before changing the default.

Private build, sanitizer and ARM evidence is saved under
`2026-09-13-worker-sizing/validation/engine-restructure-20260914T2300Z`, using
the `index-reference-neon` artifact prefix. No game captures or packages are
included in the source commit.


The first real emulator comparison completes in the Pillar of Autumn cryo room
with matching camera checks. Measured off windows report zero large NEON batches;
the on windows report 4860 per 60 frames out of 11280 reference copies. The
configured off state returns afterward. All phases meet the emulator's 20 FPS
cap; these numbers validate activation/restoration and do not establish a
hardware improvement. The retained source/package identity is runtime SHA256
`e1c13e74d72df034dae85c745dea02873126d7d77347704369e3fb203cf926f7`.


A second isolated emulator run sets `XV_DRAW_SCAN_NEON=1` before startup and
keeps the existing core-0 vertex preparation enabled. Normal Pillar startup,
turning, walking into the cryo-room pod/wall and reversing complete with the
new path active; a later 60-frame window records 6360 large NEON batches among
14760 reference copies. Screenshots and the full log are preserved. This is
limited cryo-room functionality, not campaign combat, driving or hardware crash
validation. The emulator configuration change does not change physical settings
or the source default.
