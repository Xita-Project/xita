# Blood Gulch guest-phase results — September 12, 2026

The new capture identifies the scene-preparation dispatcher and object-update
region as the largest selected regions on the guest thread. It also measures
noticeable overhead from writing the diagnostic itself. This was ordinary
gameplay with changing views and activity, not a matched FPS comparison.

## Capture and interval selection

The installed executable matches diagnostic source `30deb24`: EBOOT SHA-256
`44a1912c972c78bd87856ac7437b5ce99139261c29f32dcc5c266bd6496cf961`.
`XV_PHASE_TIMING=1` is present. The copy contains 86 complete, usable phase
windows (5,160 frames), with no dropped or invalid accounting records. One
incomplete trailing window is excluded. Its presence does not establish a
crash. Earlier rotated logs contain no phase windows.

The complete run includes menu and map-copy/loading frames. The table below
uses 70 windows ending at frames **1,021–5,161** (4,200 frames), after the first
world-rendering window at 961. Earlier loading windows render only two draws
and perform no object updates. Blood Gulch map opens and nearby game-state
records establish the context; activity can still vary within each window.

| Selected measurement | Mean ms/frame | Median ms/frame |
| --- | ---: | ---: |
| `scene_5D410` selected self time | 43.340 | 40.405 |
| `tick_900E0` selected self time | 24.777 | 24.331 |
| Main-loop inclusive active time | 80.851 | 81.011 |
| Main-loop inclusive parked time | 6.574 | 3.257 |
| Phase-report writing/formatting cost | 2.916 | 2.783 |

The two selected self regions include their **unselected callees**; these
numbers are not the cost of just each dispatcher's own instructions. Main-loop
active time already contains both regions. Do not add the parent row to them.
Active measures scheduled elapsed time and can include native blocking and
host preemption. Parked measures explicit guest handoffs. Neither is a direct
CPU-cycle or GPU-utilization counter.

The object region is entered about **2.705 times/frame**, costing about
**9.159 ms per entry** in aggregate. The game can perform multiple simulation
updates between rendered frames. Reducing rendered-frame cost can change that
ratio; skipping simulation updates would change game behavior.

Nearby draw-preparation reports average 10.257 ms/frame across their eight
fields, including 3.016 ms streams, 1.529 ms textures and 1.432 ms state. These
are nearby intervals, not an exact join with the phase boundaries, and overlap
scene work. The 351 corresponding CPU samples have median C0/C1/C2 busy values
of 4/10/86%. GPU notification completion latency averages about 49 ms across
nearby reports; it overlaps guest/submission and includes completion observation
delay, so it is not additive time or a pure shader-runtime measurement.

Configuration in this run includes 360p, 500 MHz, texture maximum 128, the vertex
worker and triple buffering, with the previously selected material/effect/LOD
settings. No settings were changed during collection. This is not a comparison
against the earlier fixed-camera worker benchmark or standard graphics preset.

## Next implementation

The actual `0x900E0` call chain walks object records and invokes state-changing
helpers. Its tail region `0x8ECA0` includes indirect callbacks and shared state.
These are unsuitable for simply running the entire loop concurrently. The
scene dispatcher calls a series of render steps and pass helpers; finer timing
is needed to choose a substantial independent range with clear inputs/outputs.

The follow-up diagnostic therefore replaces minor outer scopes with the
immediate children of those two regions, retaining the main parent boundaries.
It uses 48 selected targets within the existing bounds. Broad function tracing
remains off and uninstrumented guest bodies remain identical.

Writing one line at a time cost about 175 ms per 60-frame report in the selected
interval. The follow-up formats a bounded report and sends it through one
file-write batch, plus the separate cost record. Console chunks preserve whole
lines where possible, and short file writes are retried. This reduces report
I/O calls; its physical time saving still needs measurement. Runtime switch-off
and ordinary generation remain available for subsequent performance comparisons.

Use the finer trace to inspect transform, visibility or object-preparation
batches before adding worker ownership. Preserve object callbacks and update
ordering until their dependencies are understood. Stable 20 FPS remains open.

## Preserved evidence

Collection backed up and hash-verified 22 files (11,102,332 bytes), including
configuration, logs and recent screenshots. Device files were only read during collection, and USB was then
safely unmounted. Artifacts are in:

`/home/birchwoodgod/xita-backups/2026-09-12-221549-guest-phase-results`

`collection.json` records hashes; `phase-analysis.json` contains complete
windows and parser findings; `phase-context.json` records the post-loading
selection; `supporting-profile-summary.json` records nearby diagnostic summaries.
Owned-code extracts used for the call-chain audit remain private in that
directory and are not included in the repository.

## Follow-up candidate validation

The 48-scope candidate builds successfully. Host ASan/UBSan tests cover nested
and parked scopes, cleanup, disabled timing, bounded overflow, generation guards,
maximum-width reports, complete batched emission, short writes and logging
failures. The actual Vita input tests also pass. Regeneration changes only scope
sites and metadata; guest bodies are identical after removing instrumentation.

An isolated Vita3K session renders the dashboard, Halo menu, Blood Gulch and the
`a10` cryo-room sequence. Solo match startup, walking, camera turns, firing,
pause/leave and campaign save-and-quit respond. The file contains **255 usable
windows / 15,300 frames**, all identical to the corresponding console windows,
with no dropped/invalid accounting. A final incomplete file window at the
operator stop is excluded; the console completed that additional window.
This establishes diagnostic functionality, not physical FPS or general crash
freedom. A separate private 42-scope experiment examines the children of object
transform and scene helpers; it is not the installed candidate.

The EBOOT is 31,120,122 bytes, SHA-256
`7333602c64066f4a51d55713b8d47ab8289f1c5bce389679148a2bb114a3b466`.
The VPK is 13,308,243 bytes, SHA-256
`a3fc238421804c8ba71c951d49aa0c61b7408f1973dffc02ecf1eed42c297c5d`.
Its ZIP integrity check passes; only `eboot.bin` differs from the previous
package's 1,585 entries. It requires no new shaders or game assets.

## USB readback anomaly

Before this candidate was installed, a new read of the previous EBOOT differed
from the earlier collection hash: 32,519 bytes changed within offsets
`0x18000..0x1FFFF`. That 32 KiB region contained unrelated title-catalog JSON.
Its file size, modification time, settings and gameplay log were unchanged.
This is observed file-content damage, not a game rendering diagnosis. Its cause
has not been established, and earlier cached readback checks cannot explain when
it occurred.

The original bytes were backed up on the computer. The new EBOOT was written to
a separate file, and the previous device file was retained as
`app/XITA00001/eboot.before-20260912.bin` before switching the launch filename.
After unmount/remount, the new EBOOT and unchanged settings match their source
hashes. However, the retained old file's first 96 KiB then matched bytes from
about 30 MiB into the new EBOOT. That is evidence of overlapping storage or
inconsistent allocation; the retained device file is **not a trusted backup**.
Do not delete it as ordinary cleanup until the storage issue has been checked,
because freeing overlapping storage could affect the new file.

Device writes stopped. UDisks' read-only filesystem check could not assess this
volume: the installed checker reports `unsupported FAT count: 2`. No filesystem
repair or formatting was attempted. All 55 save/cache files (664,118,691 bytes) were copied read-only and
hash-verified on the computer. USB was safely unmounted. The candidate's current executable readback
is correct, but that does not certify the rest of the card or future writes.

Candidate, validation, USB evidence and computer backups:

`/home/birchwoodgod/xita-backups/2026-09-12-222123-phase-followup`
