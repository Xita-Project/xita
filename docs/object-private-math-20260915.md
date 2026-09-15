# Private math in experimental object workers

The preceding physical Blood Gulch comparison measured 10.077 / 5.250 /
9.963 FPS with whole-object workers off/on/off. Matrix and quaternion callers
accounted for most recorded contention. These measurements describe one fixed
view, not campaign or driving performance. See
[the frame and lock audit](object-lock-frame-diagnostics-20260915.md).

The experimental worker build now snapshots inputs and shared bookkeeping
under the existing recursive guard. Point transforms, matrix multiplication,
quaternion conversion and object basis preparation can release that guard for
the calculation when every output belongs to the current worker's guest stack.
The check includes scratch spills, stack bounds, the actual worker context and
the original mapping of every covered page. Physically discontiguous stacks are
supported; remapped, foreign and shared outputs keep the guard.

An enclosing protected transaction always retains its lock. The optional shared
quaternion cache also retains its full transaction. Matrix NEON admission and
counters run before release; the vector calculation consumes local snapshots.
No shared counters, caches or lazy configuration are updated after release.
An unlocked worker remains active in the owner-service protocol until it parks
at a normal boundary, so the owner cannot treat private calculation as a
quiescence acknowledgement.

`XV_OBJECT_PRIVATE_MATH=0` disables early release. The remote `object-math`
benchmark compares off/on/off while keeping the object workers, graphics and
other math settings unchanged. It requires active object workers and the fast
recursive guard policy. Completion, cancellation and lost first-person control
restore the configured policy at a drained frame boundary.

The `[object-private-math]` counters report attempts, releases and reasons for
keeping the guard, separately for each lane and helper. These are call counts,
not saved microseconds. The `[object-locks]` startup line now includes the native
address of `xv_object_math_lock` for resolving relocated physical profiles.

## Validation

- Original-code host comparisons: 120,000 matrix/quaternion cases, 12,288 point
  cases and 4,096 object-basis cases, including full context and scratch memory.
  Disabled modes and existing layout guards also pass.
- Actual Vita ARM executables: 10,240 full-state comparisons against the previous
  executable across float edges, random values and bounded NEON inputs; 1,536
  original-region object-basis comparisons over 16 FP modes.
- Production worker pool: 600 callbacks and 2,400 math result/context/spill
  comparisons per configuration. Two, one and zero workers, disabled release,
  and disabled fast guards pass normally, with ASan/UBSan, and with TSan. A
  rendezvous requires both workers to have released the guard. Shared, foreign,
  nested and remapped outputs are rejected.
- Existing owner audio/I/O handoff, remote protocol, frame acquisition and
  benchmark restoration checks pass.

These tests validate the new ownership boundary, not every shared dependency
inside Halo's whole-object callbacks. The latter remains experimental. A
physical comparison is required before claiming a performance gain; emulator
frame rates and instruction counts do not establish one.
