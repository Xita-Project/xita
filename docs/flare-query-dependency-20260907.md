# Flare query dependency audit — September 7, 2026

There is a potential overlap opportunity before Halo issues its next visibility
queries: the observed Blood Gulch view records 59 renderer commands after the
old-result update, and the cryo bay records 93. Exploiting it requires separating
the old result metadata from the next frame's flare list and preserving several
earlier dependency barriers. These probes preserve guest order. The subsequent
[deferred-result implementation and comparison](flare-defer-20260907.md) now
test that opportunity privately; the hardware executable remains unchanged.

The [latest hardware collection](hardware-20260907-vertex-resident.md) spends
roughly 23–25 ms per guest frame waiting for exact visibility completion in its
final stationary windows. Resuming after completion costs tens of microseconds.
These intervals overlap other work. Even eliminating this entire wait would not
alone reach the 50 ms/frame budget from the roughly 134 ms/frame observation.

## Call and data dependencies

The preserved translation calls `0x60460` at `0x80225` inside render-begin
`0x62690`. The result loop reads the count at `0x2E34E0` and 40-byte records at
`0x2C76D0`. For each positive rectangle area (`record+0x24`), helper `0x63460`
waits for that list index's visibility result. It converts coverage to a byte,
updates brightness with asymmetric smoothing, then resets the list count.

Destination identity comes from fields `+0x1E`, `+0x20` and `+0x22`, addressing
brightness storage based at `0x27FFB0` or `0x2BFFD2`. A numerical query ID alone
does not identify a flare across frames. The list can hold 1,024 records; the
separate 384-entry queue at `0x2E34C0` is not this visibility list. Current GXM
result capacity is 512, so a replacement must retain explicit capacity/error
handling rather than assuming all guest indices fit.

| Boundary | Dependency |
| --- | --- |
| `0x5FE80` | Appends next-frame records to the same list. Retain the old metadata separately before allowing this. |
| `0x5FF86` | Clears brightness storage when a cache entry changes identity. Finish old updates before that clear. |
| `0x60560` | Renders the next query rectangles and writes their areas. Complete old reads before issuing replacement generations. |
| `0x606B0`, `0x5FE30` | Render flares and locate their brightness bytes. Results must already be applied. |
| `0x80570`, `0xEC680` | Reset brightness arrays and the list. Finish pending updates before reset. |
| Next render-begin / Present | Do not let pending work escape into another frame or overwrite retained metadata. |

The original result loop also changes volatile registers, flags and stack
scratch. Its enclosing function overwrites EAX, ECX and flags, but EDX can escape
its trailing helper's early return. A guarded implementation still needs complete
caller liveness proof or eager fallback for unsupported paths. Cooperative waits
also allow other guest fibers to execute: pending work must not permit a second
fiber to cross a dependency barrier while the first is draining results.

## Private runtime observations

Two observation-only builds run in the private Vita3K environment. They preserve
the installed guest order, shaders, queue policy and standard graphics settings.
The first measures the earliest listed barrier; the extended probe measures each
boundary separately. It records command-list entries, **not GXM draw calls**.

| Captured view | First append | Replacement-query boundary | Example elapsed gap to queries |
| --- | ---: | ---: | ---: |
| Blood Gulch, facing the base | 6 commands | 59 commands | 2.86–3.28 ms |
| Cryo bay after skipping the opening | 1 command | 93 commands | 6.01–7.19 ms |

The example times are four later logged observations in each fixed view, not
physical-Vita timings or a speedup measurement. The emulator remains capped at
20 FPS. The original wait can include delayed submission from frame pacing as
well as GPU completion; it must not be interpreted as exclusive GPU time.

Firing reaches the identity-reset boundary after six commands. Campaign opening
views also reach it. A proposed optimization must finish early in those cases,
even if that reduces overlap. Waiting until brightness lookup would be too late
if newer queries had already replaced the old generations.

The menu, Blood Gulch, movement/firing/flashlight, campaign opening and cryo bay
after skip are captured. Sampled geometry checks find no vertex-byte changes
before completion. They check ownership, not whole-frame visual equivalence.
No normal Finish calls, fence failures, upload failures or draw-storage drops
are logged. Both private instances are stopped and their prior executable and
configuration restored. The authoritative staging manifest remains unchanged.

## Checked arithmetic and next implementation

A compact integer brightness formula agrees with the actual preserved `0x60460`
loop in **161,440 ASan/UBSan cases**: both destination tables, every previous
brightness byte, positive/zero/negative areas, extreme pixel values and wrapped
32-bit multiplication. This proves the formula only. Deferred scheduling,
aliased destinations, register restoration and barrier coverage remain to test.

The follow-on prototype uses a bounded snapshot of the old query index, destination address
and rectangle area. Keep eager execution for already-completed results and
unsupported cases. Apply exact results at the earliest required barrier, then
compare rendering, lifetime behavior and hardware frame times. Retain the
current installed vertex-comparison build until its requested benchmark is
collected; this audit has not changed the Vita executable. See the linked
implementation report for the completed controller tests and private comparison.

Evidence archive:
`/home/birchwoodgod/xita-backups/2026-09-07-190954-flare-dependency/`.
It contains both probe binaries, source changes, launch/cleanup records,
`extended/validation/window-analysis.json`, screenshots and the arithmetic
reference/fixture under `intensity-proof/`.
