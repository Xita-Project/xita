# Exact flare query overlap — September 14

This optional experiment lets Halo prepare new visibility queries before waiting
for earlier flare results. It retains the exact query generation, area and
brightness destination. It does not reuse approximate results or relax GPU
ownership of vertex, texture or frame storage.

## Dependency and implementation

The existing deferred flare path often drains at its first query barrier because
Halo reuses numerical query IDs. A reused ID previously made the old generation
inaccessible through the ordinary result API. The build option
`XV_FLARE_QUERY_OVERLAP=1` adds four completed-generation records per query slot.
Publication uses atomic fields and a sequence check so readers cannot pair a
generation with another generation's pixel count.

Only the fingerprinted Halo 3925 deferred-result caller retains generations.
Query barriers may be postponed while such a batch is pending. Brightness reads,
identity changes, resets, the next deferred list and Present still drain it.
In particular, a later frame cannot submit and overwrite the retained completion
history before it is consumed. Missing generations fall back before modifying
guest state. Ordinary visibility reads continue to require the newest generation.

The audited query routine `60560` builds rectangles from the new list. Its normal
call path decodes a direction, projects rectangle coordinates and records GXM
visibility draws; brightness consumption remains guarded separately. Existing
game-version checks and all non-query barriers are retained.

The option is excluded from ordinary builds. In a build that includes it,
`XV_FLARE_QUERY_OVERLAP=1` enables it at runtime; omission means off. The remote
benchmark selector is `flare-query-overlap`, using off/on/off phases and restoring
the configured setting. It rejects comparisons when exact deferred flare reads
are disabled or stale-result mode is selected.

## Validation

- Header tests cover pending and overwritten generations, arbitrary IDs, serial
  and publication-sequence wrap, and 500,000 concurrent publications. ASan/UBSan
  and ThreadSanitizer pass.
- Deferred-read tests cover query-ID reuse, late completion, every remaining
  dependency barrier, changes of setting while pending, restoration and missing
  generations. They pass with the runtime setting enabled, disabled and absent.
- The existing differential flare fixture passes against the lifted original,
  including 1,024-record batches, aliases and competing cooperative fibers.
- The real HLE/recorder/completion fixture passes in the validated shader stage,
  including retained-generation reads and waits after an ID is reissued. Its five
  runs each exercise 200,000 threaded handoffs.
- Frame acquisition, benchmark admission/restoration and real HTTP selector tests
  pass. Without the build option, the preprocessed visibility header is identical
  to the preceding version.

Three emulator off/on/off trials entered ordinary solo Blood Gulch and completed
at the 20 FPS cap. Counters confirm retained batches and postponed query barriers.
These establish integration, not physical performance. The final package also
adds tested admission checks for incompatible settings.

## Physical Vita comparisons

The verified runtime was
`e90ea4c1608f72ef0694efb828a48b409d442abe943ab1fbe09c5f98dfa5c2d7`.
All nine off/on/off trials passed the stationary-camera check. Each phase settled
for 60 frames and measured 120. Live simulation continued, with identical remote
status polling in all arms. This is a same-session comparison, not deterministic
replay or a complete campaign benchmark.

The device retained its existing 360p, 128-pixel texture, triple-buffer and quality
settings. The frame cap was 30 FPS; reported clocks were 444 MHz CPU and 222 MHz GPU.
FPS below is calculated from pooled elapsed time, with 720 off and 360 on frames
per view; frame savings average each trial's two off arms against its on arm.

| Blood Gulch view | Off FPS | On FPS | Mean frame time saved | Individual trial savings |
| --- | ---: | ---: | ---: | --- |

| Facing base | 15.56 | 16.53 | 3.787 ms | +4.542, +2.987, +3.832 ms |
| Turned toward wall/ramp | 20.35 | 21.98 | 3.646 ms | +1.577, +5.021, +4.340 ms |
| Across valley | 12.45 | 12.53 | 0.482 ms | -0.299, +1.148, +0.597 ms |

The first two views consistently improved. The wide valley was nearly unchanged
and included one negative trial. Descriptive 60-frame flare counter windows
recorded inside the measurement phases show roughly 3.14 to 0.34 ms of waiting
near the base, 12.00 to 9.28 ms in the turned view, and 0.11 to 0.00 ms in the
valley. Counter windows do not exactly share the benchmark's phase boundaries;
these support the mechanism but are not independent frame-time measurements.

Keep this available as an opt-in experiment while campaign and moving-camera
coverage grows. The measurements do not establish stable 20 or 30 FPS gameplay,
and do not justify a global default change. Query overlap cannot recover waits
that are already hidden by other work. Next work must address the remaining
CPU preparation and rendering costs in broad views.

Private logs, screenshots, elapsed-time analysis and package receipts are under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z/flare-query-overlap`.

## Campaign follow-up

The CPU-log correction package
`5e5e120eeea33e0f11a51ac9f63b05f69df526c99424a7883c5c9f8039f06619`
booted through the updater on both emulator and physical Vita. Ordinary campaign
menus entered the Pillar of Autumn cryo room. Three emulator comparisons completed
and restored the option; they are integration checks, not hardware speed claims.

Three physical cryo-room comparisons measured 6.133 FPS off versus 6.113 FPS on,
a mean **0.543 ms/frame regression** (individual changes: -0.692, -0.898 and
-0.039 ms saved). Flare waits were already nearly zero. The option remains off by
default. This result reinforces that the Blood Gulch gains cannot be generalized
to campaign performance.

The room records roughly 430 draws/frame and 35 ms/frame in vertex-stream
preparation, with core 2 frequently near full utilization. The final GPU
completion latency is about 63 ms but overlaps other work; it is not an extra
63 ms to add to CPU time. Frame-slot acquisition reported no busy-slot waits.
The next checks are indexed vertex validation and presenting-thread affinity,
followed by identifying an immutable boundary for heavier preparation work.
