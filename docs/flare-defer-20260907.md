# Deferred exact flare results — September 7, 2026

This candidate moves the old flare-result update from render-begin to the first
operation that needs it. It is a prototype, defaults **off**, and is **not
installed on the Vita**. Physical performance and driving stability remain
unverified. The installed vertex-comparison benchmark is unchanged.

The [dependency audit](flare-query-dependency-20260907.md) identified draw
preparation between the old wait and the next query. The latest physical capture
spent about 23–25 ms per frame in exact visibility waits, within roughly 134 ms
frames. Overlapping part of that interval is useful to test but cannot by itself
meet the 50 ms target.

## Implementation

`recomp/kernel/xk_flare.c` retains up to 1,024 pairs of brightness destination
and rectangle area in 8 KiB. Their list indices identify the original queries.
The next frame can overwrite the old list after its count is cleared. Results
are read from the existing backend, with its event wait and bounded polling
fallback; no GPU allocation or queue-depth change is introduced.

The controller applies the original integer coverage and smoothing formula in
list order, including aliased destinations, nonpositive areas and failed-query
output. It completes pending reads before issuing any replacement query,
reading or resetting brightness, Present, or capturing another batch. A second
cooperative guest thread reaching a barrier parks until an active drain finishes.
It never substitutes a previous query generation or last frame's coverage.

The emitter and selective updater share eight hook sites and an exact original
XBE SHA-256 guard. Native entry is restricted to three audited calling paths;
the uncertain zero-view path remains eager. Unsupported destination ranges,
counts and already-ready batches also keep the original routine. The native
helper only changes ESP and the documented list/brightness memory, without
executing guest code on a borrowed stack.

`XV_FLARE_DEFER=1` enables the prototype. `XV_VIS_STALE` must remain zero.
This candidate's L + R + Square comparison selects eager/deferred/eager at a
fixed resolution, with 60 settling and 120 measured frames per phase. Its logs
use `[flare-compare]`; the currently installed build still uses
`[vertex-compare]`. Completion, cancellation and lost view restore the configured
default after retiring pending results. Shader quality, vertex comparison and
single-flight submission remain fixed across the new comparison.

## Validation

- 600 differential batches per wait mode agree with the preserved original
  result loop. Cases cover both tables, aliased destinations, 1,024 records,
  errors, overwritten current-frame lists, eager fallback and context retention.
- Event waits, timed polling, zero-delay yielding and fixed backoff pass, as
  does refusal to combine deferred results with stale-result mode.
- Real Begin/EndVisibilityTest and Swap entry points retire results before their
  backend callbacks. Present's guard precedes pacing and submission.
- Two cooperative test fibers verify that a brightness reset cannot pass a
  drain waiting for the GPU. ASan/UBSan runs pass; ASan emits its documented
  `makecontext`/`swapcontext` support warning, so this is not exhaustive sanitizer
  coverage of arbitrary fiber stacks.
- Instruction-level liveness checks from the original XBE establish that EAX,
  ECX, EDX and arithmetic flags are dead before use on the three accepted paths.
- Traced and untraced generator output equals the selective updater at all eight
  sites. Reapplication is idempotent, a changed image disables hooks, and an
  incomplete snapshot is rejected before any source write.
- Existing visibility-generation, frame-acquisition, benchmark restoration and
  scheduler notification checks pass. The native build passes. Root generated
  sources also contain the guards, with all surrounding pre-existing code
  preserved byte for byte; the root differential checks pass separately.

## Private runtime result

The candidate runs through the menu, Blood Gulch, movement and firing, campaign
opening, cutscene skip and the cryo bay. Flashlight input and camera movement are
also exercised. Captures document these limited views; they are not a claim of
whole-frame equivalence or of fixing the historical hardware crash.

| Fixed camera, 544p, standard settings | Eager before | Deferred | Eager after |
| --- | ---: | ---: | ---: |
| Blood Gulch facing the base | 19.974 FPS | 19.971 FPS | 19.971 FPS |
| Cryo bay after opening skip | 13.543 FPS | 14.445 FPS | 13.406 FPS |

Both comparisons pass the camera check. Blood Gulch is constrained by the
20 FPS cap. The cryo-bay observation improves about **7.2%** against the pooled
eager baseline, approximately **74.22 → 69.23 ms/frame**. This is one emulator
comparison with animated scene content and about 1% drift between the baseline
phases, not a physical-Vita result.

Runtime counters confirm that the deferred path executes, retaining up to 344
records in the measured cryo view. Two full reports during its enabled measured
phase place about 5.64 and 5.75 ms/frame of elapsed work before the barrier.
Those gaps are not exclusive GPU time or a direct measurement of time saved.
An earlier identity-reset barrier is observed during firing and campaign.
No unsupported cases are logged in this run.

The two sampled geometry checks cover 99 and 576 draws and report zero changed
vertex/index data before completion. There are no logged fence errors, upload
failures, draw-storage drops or ordinary Finish calls. Queue depth stays at one,
upload high water is 1,577/8,192 KiB and the maximum logged draw count is 675.
The private emulator is stopped and its prior executable/configuration restored.

Native SELF: 34,799,754 bytes, SHA-256
`ad330d53c7408a8f0eb05f2f8c73c57062293944f63543ccf131f3f6aff98cf7`.
It is archived for review, not deployed.

## Next hardware step

At 20:21 CDT a fresh read-only USB check confirms that the installed executable
still matches the vertex-comparison candidate (`4663b025…`) and standard
configuration (`8faaa4d0…`). The log is unchanged from 18:39, so there is no new
vertex-comparison result to analyze yet. USB is safely unmounted. Collect that
already-installed comparison before changing the hardware executable, then test
this independently measured flare candidate. The user's last run involved no
Warthog driving; driving stability and sustained physical 20 FPS remain open.

The authoritative prior staging tree is preserved. The isolated candidate is
`/tmp/xita-flare-defer-build`; source manifests, a complete candidate patch and
validation artifacts are archived under
`/home/birchwoodgod/xita-backups/2026-09-07-200706-flare-defer/`.
