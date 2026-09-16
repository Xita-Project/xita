# Parallel preparation experiments

The physical world-preparation capture identifies character poses, visibility
queries and material/draw processing as substantial costs. These are selected
elapsed-time scopes, including descendants without their own selected scope.
They are not independent CPU instruction counts or interchangeable worker jobs.

## Pose-loop guard

An explicit `XV_OBJECT_POSE_EXPERIMENT=1` build with experimental object jobs
adds a disabled-by-default comparison: remote benchmark `object-pose`.
It acquires the existing recursive guard once around `8E0F0..8E5D0` in both
generated entries, `8DDF0` and `8E087`. Original arithmetic, nested helper calls,
park acknowledgements and owner-service restrictions remain intact. A local
cleanup token also releases the guard on normal early returns.

The entire pose function is not locked: earlier indirect callbacks stay outside
the region. Admission requires a running batch, the actual native worker,
its exact live context and the existing worker marker. The comparison requires
the lightweight backend and recursive fast path. Owner services carrying a
worker context cannot acquire this new scope. Only the drained initializing
owner can change the override; restoration disables it.

Host, ASan/UBSan and TSan each pass 1,152 generated-loop comparisons across two,
one and zero workers and a disabled fast path. Both owner-service rendezvous
directions, complete guest state/memory/FP comparisons, signature rejection,
cleanup, setter rejection and existing fatal budget/unsupported-call behavior
are covered. The existing production worker suite also passes. An unrelated
cold lazy diagnostic initialization race was retained separately; the fixture
initializes that diagnostic on the owner before dispatch, without suppression.

Synthetic acquisition counts change from 5,089 to 2,618 and return to 5,089 after
restoration. This establishes the intended mechanism, not a hardware speedup.
Holding the guard across more arithmetic can reduce parallel progress; the
physical comparison must include joined batch time and overlapping worker waits.
The integration build retains ordinary graphics and all previously enabled
optimizations. Guest phase timing and native hierarchy stay disabled for this test.

### Physical result

Runtime `5bed4a6040baffd8733a455ae7050dd21a6c259114f81af38e7db3f79c2c6931`
was installed and boot-confirmed through the Wi-Fi updater. All 1,588 package
members and the existing asset contract are preserved; only the executable and
boot record change. The preserved A slot remains available.

Three fixed-view Blood Gulch trials use native 960×544, unchanged saved graphics,
60 settling and 120 measured frames per arm. All camera checks pass:

| Trial | Off before FPS | On FPS | Off after FPS |
| --- | ---: | ---: | ---: |
| 1 | 10.732 | 10.759 | 10.762 |
| 2 | 10.722 | 10.695 | 10.652 |
| 3 | 10.661 | 10.609 | 10.671 |

The experiment executes about 250 pose scopes per frame and retires every scope,
with no rejected jobs. Across 180 complete counter frames per arm, outer lock
acquisitions are 3,122.68 / 1,287.74 / 3,153.78 per frame: approximately 59% fewer
when enabled. Joined batch time is 32.380 / 31.769 / 32.666 ms/frame. The workers
still each spend roughly 13–14 ms/frame waiting, with overlapping waits.
Counter windows exclude the first report after measurement starts to avoid
mixing settling frames; they cover half of the full FPS measurement.

**There is no meaningful whole-frame improvement. The override is restored OFF.**
Fewer acquisitions do not establish independent work or remove the time spent
inside shared transactions. These stationary tests do not validate driving,
campaign combat, or the outstanding crash reports. The next larger candidate
separates immutable spatial queries and private visited/result buffers from
ordered list removal, allocation and publication; that ownership split is still
being prototyped and is not in this build.

## Candidates kept separate

- A five-island native material-state prototype passes host and ARM differential
  checks, but saves only about 182 modeled ARM instructions per full path,
  excluding the actual OS thread check. Its runtime-disabled checks cost about
  556 instructions per path. It remains on an isolated research branch and is
  omitted from this hardware build. The reserved remote comparison rejects an
  unavailable implementation without changing settings.
- A private polygon-edge prototype preserves complete guest state, mapped-memory
  aliases, exceptional comparisons and original scheduler handoffs. Avoiding
  unused incoming x87 loads reduces selected modeled ARM instructions from
  122,153 to 101,860 across 28 fixtures; this is not a cycle or FPS measurement.
  The zero-count case remains slower. All 4,096 host fixtures, sanitizers and
  768 ARM fixtures pass, with additional mutated-handoff checks. It is not part
  of the installed game.
- Periodic reporting has occasional long synchronous stalls. A bounded native
  writer is a separate hitch-reduction prototype; it must preserve owned bytes,
  ordering, immediate crash logs, backpressure and updater drain guarantees.

Private evidence is under `engine-restructure-20260914T2300Z`: the three
`agent-*` audit directories, `pose-pipeline-build`, and subsequent physical
receipts. Generated game code, original bytes, captures and packages stay outside
Git. No stable 20 FPS result follows from these host checks.
