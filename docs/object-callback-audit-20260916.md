# Object callback lock audit

Native-resolution campaign captures identified `4C980` as the largest sampled
outer worker lock holder: 62.6% and 78.0% of sampled held elapsed time. Those
percentages are not whole-frame CPU time or an expected speedup. The baseline
remains about 11.5–11.6 FPS in the captured campaign room.

## Why the whole guard cannot simply be removed

The supported executable's 976-byte callback span contains 23 distinct direct
child calls. Its children include object movement, orientation, animation and
collision work. Parent-local stores alone do not establish independence:
queries can read other objects, and indirect stores can target shared data even
when the function has no direct global stores.

One concrete dependency is `4C980 → 4B9D0 → 1721B0`. The last function updates
shared query state at `2D2FAC`, `2D2FA9` and `2FC684`, writes the visited table
rooted at `2D2FB0`, and traverses lists reached through `2FC6A0`/`2FC6A4`.
The query also calls `171AF0`, which follows object data and child relationships.
Moving a mutex around only one low-level BSP traversal would leave these other
dependencies exposed. Earlier concurrent startup failures were the reason for
the broad callback guard; that evidence still applies.

Some of the work has a more promising boundary. `88E90` builds its query state
on the caller's stack and calls the BSP traversal `88B80`. Establishing a safe
parallel boundary still requires checking its full descendant reads/writes,
input vector and filter ownership, and map lifetime. A read-only-looking
function body is insufficient by itself. No guard has been removed by this
audit.

The direct-call closure for this static BSP path is `88E90`, `88B80`, `889E0`,
`86E20`, and `17ADD0`, plus the existing native plane-interval helper. The three
leaf routines write only their guest stack storage. `88B80` also writes its
query context and result buffer, while reading the collision BSP and the
breakable-surface filter. These ownership requirements must be checked at the
caller; the filter cannot be assumed immutable just because the geometry is.

## Sample the children without changing ownership

`XV_OBJECT_HOLD_PROFILE=1` now also instruments the direct child calls within
the signature-checked `4C980` body. Benchmark 42 (`object-holds`) retains its
OFF/ON/OFF behavior and restores the previous setting.

- Only the existing approximately 1-in-64 outer worker hold sample can time a
  child. Nested guarded callbacks and recursively entered child scopes decline.
- The child duration includes its descendants and any owner-service parking.
  Child totals are subdivisions of the parent's elapsed time, not additional
  frame time. They should not be added to the parent total.
- Records are fixed-size and owned by one worker. Reporting and reset happen
  after the join. No new lock, guest-state modification, allocation, or lock
  release is introduced by the timing calls.
- Ordinary builds omit the instrumentation. In a diagnostic build with sampling
  disabled, each instrumented child has a mode check but takes no timestamp.
  This residual cost means the build is a diagnostic, not an optimization.

New `[object-hold-child]` records identify the child by its guest address.
The measurements select the next replacement or lock-scope change; they do not
establish that the shared object simulation is safe to run without its guards.

## Validation

The owned-image check reproduces the exact installed callback (including the
existing guard placement), verifies all emitted child IDs, strips the diagnostic
to recover the original body, and rejects modified executable signatures.
The actual worker-pool fixture tests sampled nested calls and owner-service
parking, checks child elapsed totals fit within sampled outer holds, and verifies
joined report/reset accounting. Host, ASan/UBSan and TSan runs pass, as does the
ordinary build without the diagnostic.

## Physical campaign result

The diagnostic runtime `c8f54f0f873c5b0c` is boot-confirmed in slot 1. Its package
changes only the game executable and boot hash; the two changed compiled objects
are the translated callback's compilation unit and the worker-pool implementation.
The displayed build timestamp remains the previous timestamp because the main
object did not change. Use the runtime hash to identify this diagnostic.

Both comparisons resumed the campaign checkpoint at native 960×544, original
material/effect settings, and effective CPU/GPU clocks of 444/222 MHz. Each arm
settles for 60 frames and measures 120; simulation continues with an unchanged
camera. Sampling was restored Off after each trial.

| Trial | Off / sampled / Off FPS | `4B9D0` samples | `4B9D0` sampled elapsed | Share of parent held elapsed |
| --- | --- | --- | --- | --- |
| 1 | 11.586 / 11.635 / 11.531 | 41 | 21,668 µs | 90.7% |
| 2 | 11.527 / 11.566 / 11.453 | 40 | 26,534 µs | 94.8% |

All measured direct children together cover 97.4–97.9% of the sampled parent's
held elapsed time. `4B9D0` contributes 93.1–96.9% of that child time. The longest
individual `4B9D0` samples are 2.63 and 2.77 ms. Periodic reporting windows can
overlap settling within the enabled arm. These are sampled inclusive durations,
not total simulation cost or saved milliseconds per frame.

This narrows the next work to the movement/collision path in `4B9D0`. In
particular, it calls the substantial solver `49600`, which reaches `864C0`,
`171F10`, and `172BF0`. Their cost and shared dependencies need to be separated
before choosing a worker boundary or native replacement. The static BSP route
through `1721B0` discussed above is only one branch; these results do not yet
prove that branch dominates the solver.

The capture completed with no reported worker stop or GPU crash and with no
logger errors. It is a stationary campaign diagnostic, not a representative
combat/driving stability test. No FPS improvement is claimed. Stable 20 FPS
and the previously reported gameplay crash remain unresolved.
