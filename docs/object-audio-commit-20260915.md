# Plasma firing: deferred audio commit handoff

The user could walk through Blood Gulch's valley before firing the plasma pistol
stopped runtime `e6d9b04d…`. The recovered log ends with an explicit experimental
worker STOP at `193C1B`, returning to `291EF`, on lane 0. This target is
`IDirectSound_CommitDeferredSettings`. The indirect chain is `C3A00 -> 291D0`;
the private stack canary is intact. This identifies the software stop in this
run; it does not establish a GPU fault or explain earlier driver crashes.

Private evidence: `physical-plasma-return-20260915T163316Z/run-1.log` under the
September 14 engine-restructure validation directory, SHA-256
`cecdcb66de08f423288369b73aa6e71b3080882dc71c86ce980ae7ae3fbd7d21`.
All four available logs were preserved before another update.

## Change

The worker bridge now hands this exact call site to the guest owner after all
other object lanes park. It invokes the existing HLE handler, which currently
acknowledges deferred settings without applying additional effects. Its existing
return value and one-argument stack cleanup are preserved. This change adds no
new success stub and does not make audio code run concurrently on workers.

The rest of `291D0` was also checked. It can fade active streams through
`CDirectSoundStream_SetVolume`, returning to `292FB`. That call has the same
stream-object/signed-volume convention as the previously admitted `2982F` site.
Both now use the existing quiescent owner handoff. Unrelated callers and nested
owner-service requests remain rejected. The new `quiescent owner deferred audio
commits` counter records actual service execution separately from volume updates.

Both experimental object workers, lightweight mutexes and the previous rendering
fixes remain enabled. Shared game-state independence remains unproven; further
worker dependencies can still surface during play.

## Validation

The production worker suite passes with two, one and zero workers, wait profiling
off/on, under ordinary compilation, ASan/UBSan and ThreadSanitizer. Each run
executes 600 deferred commits, half inside held shared transactions and half
outside. Checks cover the servicing thread, publication of parked workers'
writes, unchanged arguments/registers, return value, stack cleanup and counter
reset. It compiles the actual existing HLE macro and invocation.

The 600 volume updates now exercise both admitted return sites, inside and
outside shared transactions, with known/unknown streams and signed volumes.
A then-unqualified commit caller (`28BB6`) and an unknown volume caller stopped
before invocation in these tests. The former was subsequently observed in real
stream startup and is now covered by the
[stream-start/refill follow-up](object-stream-start-20260916.md).

The native build completes without warnings. All 1,588 package members match the
updater contract; only the executable and boot marker differ from the baseline.
Runtime SHA-256:
`d44b99363d3a83d83fd490250be9cbf28117d8c7c14b6ee60f9859c66f444261`.
Physical firing stability remains to be verified. No FPS gain is claimed for
this handoff fix.

## Performance observation and settings

The user reports approximately 14 FPS average and around 15 FPS while walking
the valley, with default graphics and what they describe as native resolution.
The captured log starts at 640×360, then records changes to 704×400 and finally
848×480, upscaled to the 960×544 display. There is no recorded switch to a
960×544 render target in this run. Texture maximum 256 and original material,
glow, particle and model quality remain selected. Triple buffering was toggled
off and back on. CPU request 500 MHz fell back to 444 MHz; GPU was 222 MHz.

The best complete 60-frame gameplay window is 13.9 FPS at the final 848×480
resolution; the subsequent window is 10.1 FPS. This supports the observation
of higher frame rates during parts of the walk, but is not a whole-session
14 FPS average or a controlled comparison. Preserve these exact logged settings
alongside the user's observation when comparing later builds. Sustained 20/30
FPS in representative gameplay remains unverified.

## Second physical run and emulator check

A second log captured before installing this fix reproduces the identical target
and return site on lane 1, object `E3FB0035`. Its SHA-256 is
`fafe954d37426ace0f3844949d89afce700ea7f72282088c0d330da193fbf5e5`, saved as
`physical-plasma-more-20260915T164431Z/run-1.log`. It is still runtime `e6d9b04d…`.
This gives direct evidence that both worker lanes can reach this owner service.

This newer run does switch from 848×480 to **960×544 native**, confirming the
user's resolution observation for the later session. There are 58 subsequent
complete 60-frame windows, ranging from 5.5 to 22.4 FPS. Total frames divided by
their summed logged game-plus-wait times gives approximately 9.95 FPS; individual
windows include 14.1 FPS. This mixed walking/settings/combat capture is not a
matched comparison, and its whole captured stretch is not a 14 FPS average.

The new candidate boots through the normal split-screen menus into Blood Gulch
in Vita3K. A normal plasma shot reduces charge from 100 to 99; holding and
releasing a charged shot reduces it to 88. Captured rendering and input continue,
with no worker STOP or fragment-uniform error. Deferred-commit and volume counters
remain zero in this emulator sample, so it does not reproduce the hardware-only
service path. Host tests cover the actual handoff; a physical firing retest is
still required. Evidence is in `emulator-audio-commit/`.

## Next performance investigation

The second run also exposes an uneven object workload during slower sections.
At one 14.1 FPS window, the workers complete 5,754/5,118 callbacks, and joined
object batches take 20.17 ms per displayed frame. At a 5.5 FPS window, the split
is 1,652/26,178 callbacks and batches take 71.17 ms per displayed frame. Simulation
passes also rise from 2.12 to 5.48 per displayed frame; these are different live
scenes, so neither callback counts nor frame cost alone prove a scheduling bug.

Using the exact failing ELF and its logged code anchor (load bias `0x49000`),
the largest wait bucket in the slower window resolves to
`xv_math_point_transform` on lane 0: 800,218 microseconds across 60 displayed
frames. These elapsed waits include scheduling and parked service time; they
identify a waiting call site, not the lock holder or exclusive CPU work. Lane
times overlap and must not be added to estimate a frame-time saving.

Next, identify the long object callbacks and their shared-math transactions,
then reduce or batch synchronization where object ownership permits. Preserve
the measured lightweight-mutex improvement and both workers while investigating
the imbalance. Detailed mapped windows are saved privately in
`physical-plasma-more-20260915T164431Z/worker-timing.json`.

## Installation

The physical updater verifies runtime `d44b9936…` in slot B1 and confirms its
dashboard boot, state 0 and no pending request. Slot A0 was restored and verified
as `7f33dee4…` before uploading, preserving that rollback build. Installation
receipts and the returned dashboard capture are saved privately in
`physical-object-audio-commit/`. Graphics settings were not changed by this
update. Both experimental object workers and the lightweight-mutex default
remain enabled. The next hardware check is normal and charged plasma firing,
then continued walking/combat to expose any further worker-service dependency.

## Physical firing retest

Runtime `d44b9936…` subsequently enters Blood Gulch through the normal profile,
map and Slayer menus on the physical Vita. Saved graphics remain unchanged at
960×544 native. A normal plasma shot into the blue-base entrance reduces charge
from 100 to 99; a held/released charged shot reduces it to 88. Both completed-frame
captures return normally and the game continues running.

The full captured log records **two quiescent owner deferred audio commits**, with
no worker STOP or fragment-uniform error. This exercises the service that stopped
both preceding physical runs. It closes the reproduction for this specific
commit handoff; it is not a claim of complete combat/rocket stability. Stream
volume updates remain zero in this sample. The game is left running at the
blue-base entrance, with controller input released.

Evidence: `physical-object-audio-commit/gameplay/`, including captures, the full
log and a result receipt with the exact runtime, log hash, frame advancement and
service counts. Performance in this stationary view is around 9–10 FPS at native
resolution; this was a correctness check, not a benchmark or demonstrated gain.
The full log SHA-256 is
`46c0586460565675f34d5b34bdeef245fa0731257cf57b5f98b632ef4f2a662d`.
