# Perf.37 material-path follow-up

After the user reported no benefit from palette buffering, the installed
`0.2.0-perf.37 / efb502b` completed the existing `guest-phases` diagnostic.
Three complete 60-frame timing windows were accepted with no invalid or dropped
accounting. The camera remained at (-29.0048,37.0536,0.6185), forward
(-0.97531,0.21200,-0.06227). The controller completed and restored its settings.

This diagnostic serializes object callbacks in all three arms; only its middle
arm enables function timers. It therefore does not measure ordinary parallel
gameplay or the benefit of the pose worker. Its off/on/off rates were
8.444/7.867/8.464 FPS. The middle arm includes meaningful instrumentation cost.

| Scope | Calls/frame | Active ms/frame | Outside other instrumented children ms/frame |
| --- | ---: | ---: | ---: |
| `54010` ordered pass and callbacks | 15.00 | 21.80 | 21.76 |
| `70110` material setup and uninstrumented helpers | 97.68 | 16.95 | 16.95 |
| `5B4A0` model preparation | 56.00 | 31.67 | 7.11 |
| `A26B0` model packet preparation | 34.00 | 23.24 | 1.40 |
| `A2380` region/part traversal | 34.00 | 21.04 | 0.95 |

These are elapsed scopes, including native waits and host preemption. The last
column is not pure function arithmetic: uninstrumented descendants remain
charged to their nearest instrumented parent. In particular, the retained
`54010` body has four indirect callback sites. Do not interpret its apparent
self time as 22 ms of list traversal. Parent and child rows overlap.

The exact retained `70110` body has 21 texture-state, nine simple render-state,
four vertex-constant, and six other state-setting HLE call sites. These are
static sites, not per-call execution counts; branches can skip them. It also
calls packet allocation, UV/fog and other helpers. Existing UV/fog reuse is
active, so merely enabling those caches again is not a new optimization.

## Next implementation boundary

Prioritize material packet/state construction under `70110` and the ordered
callbacks under `54010`, ahead of another rewrite of outer `A2380` traversal.
Determine which repeated setup can be batched while retaining guest scratch,
register continuation, callback ordering and live constant updates. Do not
cache a whole guest stack packet or sort blended passes indiscriminately.

The serial diagnostic also motivates a controlled object-worker on/off check:
its rate is higher than the preceding parallel capture, but those captures are
not simultaneous or a valid worker comparison. Check that suspicion before
attributing the difference to contention or changing the core policy.

Private evidence: `../pose-pipeline-hardware/model-cost-trace/` contains the
complete controller receipt, before/after captures, raw log, parsed phase
summary and retained function extracts. No game code is included in this note.
No new runtime was installed by this investigation.

## Follow-up and testing workflow

The object-worker off/on/off capture completed at 8.413/8.195/8.351 FPS,
with matching camera checks and settings restoration. This small difference
does not justify a core-policy change or explain all of the earlier slow view.
Its receipts are in `../pose-pipeline-hardware/object-worker-contention/`.

The user reiterated that automated FPS comparisons should stop. Subsequent
performance qualification must use a deployed build, a full application restart
and actual gameplay with logs. Do not use the comparison controller as a gate
for retaining cumulative changes. Local state-equivalence tests remain useful
for preventing correctness regressions before deployment.

A source candidate retains two exact UV values instead of one, allowing
alternating materials to reuse results. It swaps owned value-bank pointers,
retains every existing input/FP/memory admission check, and invalidates both
banks at the existing scene/Present boundaries. Added tests exercise alternating
keys, third-key eviction and retirement of the non-current value. This is not
yet installed or an established FPS improvement.

The final pointer-bank implementation passed 245 ARM state-equivalence,
caller-publication and lifetime checks against the retained original routine.
These compare guest memory, context and FP status with synthetic caller inputs;
they do not establish full-game performance or hardware stability. Receipts:
`../uv-victim-pointer/cross-production-receipt.json` and its referenced results.
