# Private point transforms without shared-guard acquisition

The bounded-wait trial regressed because it sharply increased contended mutex
handoffs. This candidate instead bypasses the guard for a point transform only
when its input matrix, input vector and output are entirely on the calling
worker's unchanged private stack. It targets the point helper identified in the
slow native-resolution gameplay wait log. It does not assume that arbitrary
objects, shared matrices or the whole game state are independent.

## Ownership and unchanged work

Admission verifies the actual native worker thread, exact live context, active
batch, private span bounds and original page mappings. A copied context or an
owner audio service using a worker's context is rejected. The stack canary,
foreign stacks, remapped pages and nested enclosing transactions cannot
qualify. Each admitted call still checks the cooperative park request before
accessing memory, so the owner can obtain quiescence for audio/cache services.

The first point call initializes and publishes immutable configuration under
the existing guard. Guarded calls retain a common counter slot; each private
worker has a separate aligned counter slot. Reporting checks that batches are
joined before combining/resetting the counters. This prevents profiling itself
from requiring a shared lock for otherwise independent calculations.

The same point arithmetic, float validation, context updates and fallback code
are retained. Existing output-only private math remains enabled. The new
`XV_OBJECT_PRIVATE_POINT` option defaults off. Remote `object-point` comparison
switches off/on/off only at drained boundaries, with the same mutex backend,
worker count, graphics and other math options, and restores the configured value.
Counters separate admitted private calls, nested transactions, shared inputs
and shared outputs. Admission counts include helper declines; they do not by
themselves prove arithmetic work was accelerated.

## Validation so far

The expanded production private-math fixture tests both point policies, both
wait policies and five worker/private-math/recursive-guard configurations. Every
run executes 600 callbacks and checks full context and memory against the same
production arithmetic under a held guard. Memory and thread sanitizers pass.
An explicit two-worker rendezvous proves an admitted point completes while
another worker holds the mutex. Another held-guard request exercises quiescent
owner audio service while its peer runs only private point calls. Wrong native
threads, copied contexts, foreign/shared/remapped spans and nested guards are
rejected; counter reporting/reset is checked. A fixture ordering problem was
fixed by waiting for both lanes to finish their guarded reference setup before
starting the held-guard test.

The original point fixture passes 12,288 comparisons in each of normal,
global-disabled and point-disabled modes. The linked ARM baseline/candidate
comparison passes 1,152 full context/arena/FP-status cases spanning float edges
and six FPSCR modes. These ARM fixtures run without object workers, so they
verify compiled arithmetic equivalence rather than private ownership. They
execute about 4.4% more instructions in this setup; instruction counts are not
hardware frame time. The ownership tests above exercise the bypass itself.

Production worker-service tests, frame-drain ordering, benchmark availability,
completion/cancellation/restoration and remote protocol checks pass. The native
build completes with the existing macro-aliasing/unused-runtime warnings. The
package contract matches all 1,588 members except runtime and boot marker.
Candidate runtime SHA-256:
`be5b7fec4d98742a528f65bbaa2ce835a890b76693ae9c1fe2b12b1fd7adac2c`.
Private evidence uses the `object-private-point` prefix in the September 14
engine-restructure validation directory.

No hardware performance gain is established yet.

## First emulator observation

The first candidate completes normal menu startup and the remote off/on/off
comparison in Blood Gulch, with no worker STOP and successful restoration. The
20 FPS emulator cap prevents an FPS conclusion. All **23,600** checked point
calls have outputs outside the private worker stack; zero qualify for the
bypass. This rejects assuming that this change will help this sampled view.
The current physical build is retained while tracing the actual write sites.

A bounded caller trace was then added to the candidate-only path. Per lane it
keeps up to 16 return PCs plus an overflow bucket, counts, and first-seen register
addresses/object/callback IDs. It reads a return PC only from a verified private
stack; it never dereferences the recorded shared object addresses. Records are
printed/reset after joining. ThreadSanitizer and count/reset checks pass. The
trace build preserves the package contract with runtime SHA-256:
`56e2798cff38c3feb1b4e044954750dc8579570602e853ca0ae9a07346939184`.
Its artifacts use `object-point-sites`; neither candidate has been installed on
the physical Vita at this stage.

## Caller evidence and decision

The trace run checks 23,848 point calls with no worker STOP. Of these, **23,577
(98.86%)** return to `8E662`; the original call at `8E65D` writes to `EBP+0x50`
in the model-update region. The captured output and matrix addresses are in
object memory, while the vector address is reused across multiple objects.
The other 271 calls return to `172FFD` inside the already-guarded `4C980`
transaction. Those use stack addresses but cannot bypass their enclosing guard.
No calls qualify for the new path in this second view either.

Evidence: `emulator-object-point-sites/comparison/sites.json` and the full log,
SHA-256 `5f1d044b639dfec27e719b22a154dbe74ca17520d6bc231048db7072557740a0`.
This is attribution of point-helper calls, not a claim that the call site is
98.86% of frame time, nor a measured hardware gain. The original caller extract
is kept privately. No synchronization was removed for the shared object writes.

The prototype now requires **both** `XV_EXPERIMENTAL_OBJECT_JOBS=1` and
`XV_OBJECT_POINT_EXPERIMENT=1` at build time, then the runtime toggle or benchmark.
Ordinary worker builds contain no admission check in the point helper, and reject
the comparison as unavailable. Existing lightweight mutexes, output-only private
math and owner-service fixes remain intact. Both compiled modes pass host tests.
The default linked ARM helper still matches the baseline for all 1,152 fixtures;
it executes 0.35% fewer instructions in that fixture set, with no slower case.
This verifies that the experimental check is absent, not an FPS improvement.

Next, audit the reads and writes around the model-update region and `object+0x50`
before moving that work into larger independent batches. Other object and
collision callbacks may read these fields. A private-stack-only bypass does not
establish ownership of them. Keep the physical `10ef0c0d…` build for gameplay;
these research binaries have not been installed there.

## Reproducible opt-in and normal build

The build tracks the point research flag for `xk_math.o` and `xk_object_jobs.o`.
Enabling it produces exactly the trace runtime/ELF hashes recorded above.
Disabling it rebuilds both objects and restores the validated normal runtime
`b399254e5258bbe2bfc69ec8b559f481b84b893179d228341b49c473b595e7ad`.
Repeating the disabled build leaves both object timestamps unchanged. The
private `object-point-build-toggle.json` receipt records these checks, and the
restored ELF matches the default ARM-validation artifact byte for byte. This
normal artifact is built and checked locally; the physical Vita still runs
`10ef0c0d…`. No device update was needed for this research result.
