# Reducing index-mask overhead — September 8, 2026

This follow-up is staged on a separate private branch. It has **not** replaced
the [installed hardware comparison build](hardware-20260908-vertex-references.md).
The indexed vertex experiment stays off by default. Neither this change nor the
installed experiment has a measured Vita FPS improvement yet.

## Change

The [indexed validation experiment](vertex-references-20260908.md) saved stream
comparison work in the private campaign sample, but added index-preparation
work. In the lighter Blood Gulch sample that overhead exceeded the stream saving.

The follow-up keeps the running maximum and group count in local variables.
For index lists of at least 256 entries on ARM NEON, it sets coverage bits while
retaining the indices and counts the completed mask with vector population
counts. Short lists and other architectures count newly encountered groups
incrementally. This avoids scanning most of the mask for a short draw containing
a high-numbered vertex.

Both paths use the same cached index chunks that are copied to owned GPU memory.
They produce the same retained bytes, maximum and exact eight-record coverage
mask. The complete mask is cleared before capture, so the vector scan can safely
round its end to four words within the 256-word array. Vertex comparison policy,
GPU snapshot lifetime, draw order and benchmark selection stay unchanged.

## Evidence and limits

Four previously captured frames contain 876 non-immediate stream-0 draws and
406,662 indices. Their 314 unique index lists were run through the old and new
helpers compiled with VitaSDK GCC using `-O2 -mthumb -mcpu=cortex-a9 -mfpu=neon`.
Unicorn 2.1.3 executed the helpers with strict memory-access checks and modeled
firmware copy/fill calls. Duplicate lists were weighted by their original draw
counts. Retained indices and all 1,032 mask bytes matched independent expected
values for every list.

| Captured frame | Previous helper instructions | Follow-up instructions | Reduction |
| --- | ---: | ---: | ---: |
| 3632 | 1,541,416 | 1,214,959 | 21.2% |
| 5872 | 2,461,815 | 1,936,612 | 21.3% |
| 8144 | 1,188,612 | 938,516 | 21.0% |
| 12320 | 919,951 | 741,378 | 19.4% |

These are executed helper instructions, **not cycles, physical memory traffic,
frame time or FPS**. Firmware copy/fill bodies, cache effects and GPU behavior
are not modeled. The vector path performs more writes to the cached mask: in
frame 5872, instrumented reference-structure writes rose from 699,640 to 844,372
bytes while reads fell from 1,367,260 to 658,460 bytes. Those counts include the
modeled clear. A simpler local-counter variant reduced instructions by about
14% and performed fewer writes; hardware must establish which tradeoff is useful.
The private capture data and scratch comparisons remain outside the source repo.

Validation for this follow-up:

- Host ASan/UBSan checks passed for exact coverage, borrowed/invalid retention
  fallbacks, referenced/unreferenced mutations, aliases, and 4,000 draws across
  500 slot generations. Earlier GPU snapshots remained intact. All 16 benchmark
  selector combinations passed completion, cancellation and view-loss checks.
- Vita-compiled helpers passed 278 capture cases and 475 total calls with strict
  read/write bounds. Added cases cover repeated values around mask boundaries,
  both sides of the 256-index cutoff, reversed full-range indices, and reads and
  writes ending exactly at the allowed buffer boundary.
- Draw-preparation/state/cache host checks passed using the private native
  build's generated shader tables. The full native build succeeded, and 87
  runtime/header/build inputs were checked against this source tree. All 1,584
  non-executable VPK payloads match the installed candidate's native package.

The new executable has not been run in Vita3K or on hardware. Existing private
emulator results describe the earlier installed implementation. The new native
VPK SHA-256 is
`60347f25cab23cac8010a5040693153f01bace0e1e86bb58bc821c0cf81ce132`.
It is a local validation artifact, not a newly published recovery release.

## Next decision

First collect the installed full/indexed/full hardware comparison, with its
executable hash and graphics settings. If indexed validation saves useful time,
compare mask implementations at the same views and settings, including total
frame time and index-plus-stream preparation. A shorter helper is insufficient
evidence to enable the experiment globally. Driving, firing and death/respawn
still need hardware stability checks.
