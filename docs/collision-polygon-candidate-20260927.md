# Collision polygon writer candidate

Status: output-page-cache experiment rejected for promotion. Perf269 is unchanged.
The packet-construction branch is still a target; no hardware FPS gain is claimed.

The preceding collection capture (vehicle-update-attribution-20260926.md) puts
polygon/capsule packet construction outside the existing native geometry query.
A first 85020 candidate attempted to reuse the output record's page translation
at 22 integer memory accesses. It retains every original store, float operation,
flag update and loop edge. Its single local page cache is invalidated after each
of four emitted preemption sites. Checked-address builds retain X_G per access.
There is no persistent collision-result cache or new concurrency policy.

`tools/prepare_collision_polygon_cache.py` prepares only a private candidate from
the exact pinned function body, rejecting source drift or unexpected site counts.
It does not install a hook or change game defaults. All generated game sources,
binaries and captured artifacts remain in `../collision-polygon-candidate/`.

## Correctness evidence

`tools/test_collision_polygon.py` and `tools/tests/collision_polygon.c` compare
full guest context, a 4 MiB arena, the page table, and full-context observations
at preemption. No collision callees are mocked: this leaf has no guest calls.
The 1,024 synthetic cases cover TOP 0–7, finite/tied/NaN/infinite normals,
empty and full packet counts, counts around the 256 cap (including negative),
0–9 input vertices, projection directions, plane/point aliases with the output,
nonidentity mappings, point-page crossings, and page remapping at actual yield
callbacks. They do not exhaust arbitrary stack aliases, active floating-point
trap modes, concurrent production scheduling or all retail gameplay inputs.

Host ASan/UBSan and Cortex-A9 Thumb ARM tests on Pi core 0 both passed 1,024
cases. A deliberately stale cache (invalidation removed) fails case 8 with an
arena mismatch after seven preemptions. The mutant confirms remapping coverage
can reject the specific bug; it does not prove universal correctness.

## Efficiency evidence and decision

ARM function sizes in the linked test: reference 0x1718 bytes, candidate 0x185e.
A separate private, uninstrumented eight-point polygon micro-workload, compiled
for Cortex-A9 Thumb and run on Pi core 0, made 300,000 calls per batch:

| Batch | Reference ns/call | Candidate ns/call |
| --- | ---: | ---: |
| 1 | 947.940 | 1002.335 |
| 2 | 946.720 | 1004.101 |

These times include identical context/count reset overhead. They measure one
synthetic shape on the Pi, not actual Vita cycles or frame-time impact. The
candidate is larger and about 6% slower in both batches, so there is no evidence
to promote it or spend a hardware deployment on it. No existing optimization
was removed; this candidate never entered the gameplay build.

The next replacement should remove translated register/x87 bookkeeping or
repeated packet work, rather than add per-access page-cache branches. Reuse the
full-state differential fixture, extend admission/alias cases for the chosen
replacement, then use representative ARM execution before hardware qualification.

## Register candidate with yield synchronization

A second candidate reuses the retained x87 register lowering for this leaf
(`x87-regs-work/regen-regs-d3-final`). Its corresponding memory baseline matches
the retained perf269 function. This is reuse of earlier generated work, not a
claim that the register lowering was newly invented or that all old code was
absent from earlier builds.

The unmodified register version failed the new fixture at case 2: final context
and arena matched, but the scheduler observed stale floating-point context at
the back edge. That variant must not be promoted based on final output alone.
The candidate now spills its three physical slots and status before an actual
yield, then reloads TOP, slots and status on return. Both loop back edges have
zero relative x87 depth. The preemption budget decrement and callback placement
remain unchanged. This change is scoped to the pinned leaf; it is not a global
recompiler fix or a finding that every register-lowered function is affected.

`tools/prepare_collision_polygon_registers.py` pins both private input bodies
and prepares this candidate without installing it. The fixture now also mutates
TOP, all floating-point slots and status during selected yields; the candidate
passes 1,024 host ASan/UBSan and 1,024 Cortex-A9 Thumb/Pi comparisons including
these mutations. The failed-test path now frees fixture allocations so a
correctness failure does not generate distracting leak reports.

The same isolated eight-point workload on Pi core 0 reports reference
951.577/950.873 ns per call and candidate 912.628/912.207 (two batches of
300,000 each, including identical reset overhead). This is about 4% faster for
one leaf/workload, not an estimated frame-rate gain. It justifies campaign
harness qualification, not declaring a hardware improvement. The private
`gameplay/` stage replaces only 85020 relative to the preceding collection
capture and retains existing timers/routes. Build/capture qualification remains
pending; no Vita deployment has occurred.

## Campaign harness and hardware package

The Pi campaign candidate completed its planned 180-second run (exit 124),
86 reports through frame 5160. The bounded fatal/signal/scope/x87-guard scan
found no matches. This is an ordinary headless run, not a runtime differential
comparison or visual verification. The recorded scripted weapon inputs alone
do not prove both firing bursts occurred. Private evidence is in `gameplay/`.

Perf270 / c8fb09c5 built successfully in `../polygon-register-hardware/`.
The maintained shard audit proves only 85020 changed relative to perf269's
code_013.c. The build also includes the previously compiled cold fragment-load
subtimers from runtime/xv_shader.c (frame-slow-diagnostic-20260926.md); these
are diagnostics, not a shader optimization. Existing build warnings match the
baseline, including the composite texture-row warning; no warning fix is claimed.

Package audit: same membership/update contract as perf269, with only
`game-a.self` and `boot-game.txt` bytes changed. Runtime: 34,818,538 bytes,
SHA-256 `28073eabb0bb3fbc0e1b98282278c39f650a732a6b9e400c04371c4beff4a7a4`.
VPK SHA-256 `42b0051e423eff50b8e10d9be3534091a7296c010ae16e77d82157a45ad10ca3`.
Contract `775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897`.
Remote upload/application has started; boot and hardware gameplay results remain
pending. Perf269 is retained as rollback. No settings or saves are changed by
the package itself.

Remote update completed: runtime hash verified, slot 1, restart requested and
boot confirmed. Independent status reports `0.2.0-perf.270 / c8fb09c5`, timing
frame 0 and benchmark off at the dashboard. Keep-awake lease renewed. The
existing batched a30 launch sequence has started using `a30-perf211`; settled
hardware measurements and screenshot validation are still pending.

## Perf270 physical gameplay results

The existing batched launch reached a30. Readiness polling waited for
loaded/active/director telemetry before measurement; the loading screenshot is
not a gameplay sample. Idle screenshots confirm the lifepod, AR and HUD. After
the five-second trigger hold, the screenshot shows the empty magazine/reload
pose; after movement, both outdoor images show the same landscape/view direction.
No crash was observed in this short sequence. Audio, AI combat, checkpoint
resumption, 15-minute active stability and the canyon cutscene are not qualified.

Existing 32 launch settings were retained, including 360p and `a30-perf211`.
The benchmark feature was off. CPU Present-to-Present intervals:

| Segment | Samples | Mean ms / FPS | p95 ms | Max ms | Over 50 / 100 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Lifepod | 720 | 59.803 / 16.72 | 75.636 | 88.152 | 643 / 0 |
| AR hold | 64 | 83.822 / 11.93 | 102.645 | 120.786 | 64 / 7 |
| Movement | 83 | 67.683 / 14.77 | 78.355 | 282.264 | 82 / 1 |
| Settled outdoors | 910 | 49.689 / 20.12 | 59.013 | 89.079 | 280 / 0 |

These do not establish a whole-frame improvement over perf269. The prior outdoor
view moved, and independent launches have different timing/scene history. About
31% of outdoor intervals exceed 50 ms despite the average exceeding 20 FPS.
The sustained gameplay goal remains unmet. Keep the qualified candidate in the
cumulative build while targeting the much larger firing/scene-preparation cost;
the isolated leaf result does not explain or solve that cost.

Slow-frame partitions again mostly precede Present. Movement frame 6815 totals
282,264 us: before-Present 271,733; publish/acquire 10,343. The remaining parts
sum exactly to the total. These are elapsed intervals including scheduling and
internal dependencies, not pure CPU time or independent GPU service costs.
Private idle/gameplay logs, status marks, screenshots and JSON summaries retain
all measurement ranges.

## Shader setup finding and follow-up

Perf270's ordinary log exposes 82 successful cold fragment-load timing records
by the end of the gameplay capture. The largest is 48,969 us, split into embedded
load 91, registration 8, link 63 and metadata 48,807 us. The next largest is
41,001 us with 40,749 in metadata. All are wall times; missing helper-suppressed
records are not proof of no hitch. Neither record establishes which operation
inside metadata dominated or that it caused the movement spike.

Inspection shows metadata includes an alpha/constant diagnostic log.
`xv_log_write` routes ordinary non-report owner calls through the immediate
console/mutex/file path. Thus calling this interval GPU shader compilation or
parameter-lookup CPU time alone would be incorrect. The final timing log's own
write is outside the interval and can still stall.

The source follow-up consolidates successful-load diagnostics into one final
record, retaining discard/constant information and leaving failures unchanged.
There is no log write inside metadata now, and the redundant discard API query
is removed. Helper suppression remains intact. The Vita unit compiles using the
retained stage headers, and the existing material-link fixture passes (it tests
cache identity/fallback/alpha routing, not actual GXM execution). Initial compile
from the source root lacked a generated header; using the intended private
build-stage include context resolved it. Artifacts: `../shader-log-consolidation/`.
This follow-up is not deployed yet and has no measured FPS or hitch improvement.

## Perf271 logging follow-up deployment

Perf271 / 7b9b849d retains the qualified polygon change and consolidates cold
shader diagnostics. It built successfully. Package membership and contract are
unchanged; only CE runtime and boot metadata differ from perf270. Runtime size
34,818,210 bytes; SHA-256
`aaa91f49bb402edffc5f0d90f4c66fc6b46b7cc166cf1ac641ad6ba9e1a245fd`.
VPK SHA-256 `f8c9c423a43e9125968f4371b1bb78495d2e4914187e521a0edb67e4da2286e0`.
Remote hash verification and slot-0 boot confirmation succeeded; independent
status confirms the version with benchmark off. Keep-awake renewed and the
protected a30 launch sequence started. Cold-load and gameplay measurements
remain pending in `../shader-log-hardware/`. The newly identified object-worker
readiness correction is not part of this package.
