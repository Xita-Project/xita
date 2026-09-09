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

The native VPK SHA-256 is
`60347f25cab23cac8010a5040693153f01bace0e1e86bb58bc821c0cf81ce132`.
It is a local validation artifact, not a newly published recovery release.

## Private emulator follow-up

The exact candidate package was installed into an independent private Vita3K
copy, with all 1,585 payloads verified. The current saved hardware graphics
settings were retained at 360p with the 20 FPS cap. Indexed validation was enabled
outside the automatic full/indexed/full comparisons; the dashboard was bypassed
and explicit single-frame geometry diagnostics were enabled. Physical Vita files
were not changed.

Both comparisons completed all three phases with 60 settling and 120 measured
frames per phase, a comparable camera, and restoration of the configured mode.

| Scene | Full before: index + stream ms | Indexed ms | Full after: index + stream ms | FPS before / on / after |
| --- | ---: | ---: | ---: | --- |
| Campaign cryo room | 5.842 | 3.846 | 5.901 | 19.949 / 19.963 / 19.935 |
| Blood Gulch spawn facing the base | 0.801 | 0.770 | 0.794 | 19.945 / 19.963 / 19.967 |

These are emulator caller-time samples under a frame cap, not a Vita performance
result. The campaign camera and enabled reference counters match the earlier
candidate's cryo-room sample: 2,580 checks/hits and 70,200 comparison runs per
60-frame report. Enabled index preparation averaged 1.084 ms here, versus about
1.542 ms in the earlier run. That is a comparison between separate emulator
sessions, not an isolated hardware comparison of the two mask implementations.
The Blood Gulch spawn camera differs from the earlier sample; only its own
off/on/off phases are directly comparable.

Checks covered normal solo split-screen start, campaign horizontal/vertical
camera movement and flashlight input, Blood Gulch rifle fire, passenger/driver
entry and exit, forward/reverse driving, camera steering, cliff collisions, and
two grenade deaths and respawns. The second death transition was captured,
including the player body, rejoin countdown and restored first-person play.
Driving consisted of short input segments near the base, including wall
collisions; it was not a sustained representative performance run. Campaign
combat and progression were not tested in this follow-up.

Sixteen explicitly sampled frames checked 3,154 draws with no retained-geometry
changes before GPU completion; the uploader reported no failures. The match
returned to the main menu and the private emulator processes were stopped.
The physical rocket/death crash remains unverified, and no hardware FPS gain is
established by these checks.

## Next decision

First collect the installed full/indexed/full hardware comparison, with its
executable hash and graphics settings. If indexed validation saves useful time,
compare mask implementations at the same views and settings, including total
frame time and index-plus-stream preparation. A shorter helper is insufficient
evidence to enable the experiment globally. Driving, firing and death/respawn
still need hardware stability checks.
