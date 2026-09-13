# Port research and the next CPU-sharing test

We have used ideas from other ports, but their reported frame rates do not
measure Xita. The useful common direction is to remove repeated preparation,
give submitted data a clear lifetime, and send independent work to persistent
workers. More occupied cores alone do not establish shorter frames.

## What has informed Xita

| Reference | Technique reviewed | Xita work and limits |
| --- | --- | --- |
| [Dusklight / Aurora review](dusklight-aurora-review.md) | Frame-owned uploads, independent dirty state, compatible adjacent draws and bounded render workers | Xita retains frame-owned snapshots and completion tracking, and has reduced repeated constant/state preparation. The specific Vita backend was not available for inspection. Arbitrary draw sorting is not an implemented optimization. |
| [Skate3-Mobile review](skate3-optimization-review-20260908.md) | Stable mesh metadata, finalized dynamic palettes, visible-work priority and native engine boundaries | This informed preparation and native-math investigations. The latest CE candidate batches model matrices, constructs object bases natively and avoids redundant constant work. Its hardware gain is unmeasured. We did not import the Android renderer or its frame-skipping policies. |
| [User-supplied Wii archive](wii-reference-20260909.md) | Authored model LODs, retained materials, offline geometry/texture preparation | The resulting [model/material work](model-preparation-20260909.md) selects existing eligible meshes earlier and reuses unchanged expanded shader colors. Most examined Blood Gulch scenery lacks alternate meshes, limiting that setting's scope. A new offline geometry cache remains future work. |

The [HL2 developer's earlier comment](https://www.reddit.com/r/vitahacks/comments/1w9ojfa/comment/p8f8r7q/)
describes a main core plus a worker and prioritizes stability. The code and
matched benchmark behind the later supplied screenshot's 4-to-60 FPS claim
remain unverified. A fifteenfold change cannot be attributed just to distributing
unchanged CPU work over three comparable cores; other changes or test conditions
would have to contribute.

One [COD4 multiplayer developer's post](https://www.reddit.com/r/VitaPiracy/comments/1wdxtxu/welp_i_was_planning_on_it_being_a_surprise/),
checked September 13, reports approximately 5 FPS in game and a near-20 Hz server
tick, with faster rendering still a target. Simulation tick rate and rendered
FPS are different measurements. This post does not establish results for the
separate campaign effort or supply a verified optimization patch for Xita.

## Why a small calculation is not automatically a worker job

The previous private object-preparation audit identified `0x5A430` as a bounded
math candidate. Its arithmetic resembles a projected-size metric; that name is
an inference, not a recovered symbol. The caller establishes an immediate
dependency:

1. `0x5B4F9` calls the calculation.
2. `0x5B4FE..0x5B4FF` writes its returned x87 value into an argument slot.
3. `0x5B502` passes that value to the preparation/cache helper `0x5AE10`.
4. `0x5B50E` calculates the metric again after that helper and record publication.

Launching only the first calculation asynchronously would require a join before
the next helper. Running all `0x5B4A0` calls concurrently would instead share
cache allocation/eviction, object links and scratch storage. Neither is a useful
drop-in worker split. Reusing the second result still requires proving that the
intervening helper preserves all inputs and reproducing guest context effects.
This audit changes no game behavior; owned instruction extracts stay private.

The [model palette batch](model-palette-batch-20260913.md) has independent matrix
products and a later output consumer. Its known coverage is limited: selected
Vita3K windows average 47 products in eight accepted batches per frame in Blood
Gulch, and 62 products in thirteen batches in the cryo room. Those averages hide
the distribution of batch sizes and do not measure hardware cost.

## Diagnostic added for job sizing

An optional `XV_PALETTE_JOB_PROFILE=1` build switch records an exact histogram of
accepted serial batch sizes, from 1 to 64 matrices. It requires the existing
`XV_NATIVE_MODEL_PALETTE=1` build and runtime option. Use a separate diagnostic
build directory. The unchanged `[model-palette]` record still reports total
accepted batches, products and rejection reasons; the additional record is:

```text
[model-palette-sizes] 60 frames sizes 2:120 32:60
```

This illustrative line means 120 accepted two-matrix batches and 60 accepted
32-matrix batches over that interval. It is not measured gameplay. Rejected
batches are excluded. Counters reset with the existing report interval, and the
histogram is formatted into one bounded log call instead of writing each bin
separately. No timing, worker dispatch, guest data, or per-call log is added.

When profiling is absent, the Cortex-A9 model helper's machine code is byte-for-
byte identical to the previous candidate (756 bytes). The diagnostic variant
is 776 bytes; the report is separate. This establishes absence of extra helper
instructions in ordinary builds, not a performance gain from the diagnostic.

With profiling enabled, all four runtime modes pass 4,096 original-code
comparisons each, full context/arena checks, 19 unchanged decline fixtures and
seven scheduler-handoff fixtures. The histogram tests cover every accepted size
1–64, excluded rejections, disabled native paths, and interval reset. Normal and
ASan/UBSan runs pass. VitaSDK compiles both diagnostic and ordinary variants.
Evidence is preserved under `2026-09-13-worker-sizing/validation` outside Git.

Before adding a worker path, collect the actual size distribution, identify the
first output consumer and snapshot cost, then test thresholds against dispatch
and join time on Vita. Tiny groups should retain the serial path. Measure total
frame time with logging disabled for the final comparison. The latest gameplay
candidate and graphics settings have not been changed by this diagnostic work.
