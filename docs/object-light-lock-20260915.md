# Comparing the object-worker mutex backend

The [private-math comparison](object-private-math-20260915.md) did not improve
physical frame rate. Its fixed Blood Gulch view still acquired the recursive
guard roughly 140,000–150,000 times per 60 displayed frames. This experiment
compares the ordinary kernel mutex with the VitaSDK lightweight mutex API.
The available calls and work-buffer type are documented in
[VitaSDK's thread manager reference](https://docs.vitasdk.org/group__SceThreadMgrUser.html).

Both implementations protect the same critical sections. The existing
per-worker recursive depth, private math policy, contention delay, owner
notifications and quiescent service acknowledgements remain in place. This is
a test of synchronization cost, not removal of synchronization or a change to
object update ordering.

The worker pool creates both mutexes before starting threads. Failure to create
the optional lightweight mutex retains the ordinary mutex and rejects the
comparison. Only a drained frame boundary can select a different implementation;
switching with an active object scope or batch terminates the experiment. The
backend stays fixed while any worker or owner can hold the guard. Shutdown
joins workers before destroying both successfully created mutexes.

`XV_OBJECT_LIGHT_LOCK=0` selects the existing kernel mutex at startup. After the
physical comparison below, the experimental worker build defaults to the
lightweight mutex when available. The remote `object-lock` comparison runs kernel /
lightweight / kernel with identical workers, math and graphics settings. It
restores the configured selection on completion, cancellation or lost control.
`[object-lock-backend]` records the selected implementation beside frame reports.

The host adapter test compiles against the installed VitaSDK declarations and
uses recursive pthread primitives beneath platform shims. It checks 16,000
protected concurrent updates, recursive and contended try-lock results, API
selection, configured restoration, creation failure, exact destruction, and
termination on unexpected errors for both backends. Normal, ASan/UBSan and
TSan runs pass. Production worker audio/I/O handoff, remote protocol, frame
acquisition and benchmark restoration checks also pass.

Host shims do not establish firmware correctness or performance. Native emulator
and physical validation are required before calling this an optimization.

## Physical comparison

The comparison candidate ran in physical slot B with runtime SHA-256
`7becdd272289b5f6cc3f8eaf9cd39f4fad2542826e77518d247108ae43000034`.
Slot A was retained. The exact candidate also passed an emulator comparison,
including the real backend switches and restoration; its capped 20 FPS timing
is not a hardware result.

Three stationary Blood Gulch trials measured:

| Trial | Kernel before | Lightweight | Kernel after |
| --- | ---: | ---: | ---: |
| 1 | 11.805 FPS | 15.060 FPS | 11.807 FPS |
| 2 | 11.961 FPS | 15.198 FPS | 11.848 FPS |
| 3 | 11.851 FPS | 15.165 FPS | 11.701 FPS |

All camera checks passed at the same blue-base position and direction. Render
resolution remained 360p with original material, model, particle and glow
settings, standard texture detail and no frame cap. Private math and both
object workers remained enabled in every arm. This is approximately a 28%
frame-rate gain in this view, not a campaign or driving result.

Fully contained 60-frame windows in the first comparison show joined object
work near 48–49 ms per displayed frame with the kernel mutex, versus about
18 ms with the lightweight mutex. Per-lane elapsed lock waits fall from about
21–24 ms to 7–8 ms. These waits overlap and include scheduling time; do not add
them together as CPU time saved. The faster frames also require fewer original
simulation passes per displayed frame, with both arms still reporting about
30 passes per second. No simulation work was deliberately removed.

The first automated menu sequence accidentally entered campaign; that capture
is not the comparison. The actual trials started only after returning through
the normal menus and verifying Blood Gulch. The startup code anchor establishes
the candidate's physical relocation before symbolizing lock sites.

These results justify enabling the lightweight backend in the next experimental
candidate. They do not establish a benefit over serial object updates.

## Default-enabled gameplay follow-up

Runtime `a8d89663d2b9afb922addde1670f60a4872a79c08e9923dbccacbe8881c0d5a1`
enables the measured backend by default. Adapter and restoration tests pass;
the exact executable reaches Blood Gulch, turns, moves and fires the plasma
pistol in Vita3K without a captured STOP. It was installed and verified in
physical slot B, retaining slot A.

On hardware, the new default reaches Blood Gulch, moves from the red base to
the central rocket launcher and successfully picks it up. Captures show the
launcher equipped and continuing frames with the lightweight backend and both
object workers active. The pickup window records nine cache yields and no
owner audio pumps. This validates one physical pickup, not all audio or cache
paths.

After a 0.3-second firing input, the completed-frame capture timed out. Subsequent
status, log and updater requests also timed out. No post-shot screenshot or
fault log has been recovered, so this is an unresolved firing failure, not a
verified GPU-driver diagnosis. No deliberate self-damage test was performed.
Vita3K subsequently completed a shot from a similar central-map position and
direction, followed by a separate downward shot, visible player death and
respawn. The saved log contains no object-worker STOP. This does not reproduce
the physical failure or validate the physical GPU path. Recover the hardware
fault log when the console is reachable again before assigning a cause.
Representative campaign and driving stability remain open.

## Recovered firing failure

After the user reopened Xita, the saved run identified a worker-lane-0 STOP at
`CDirectSoundStream_SetVolume`, target `193D4F`, return `2982F`, with an intact
stack canary. The [stream-volume owner handoff](object-stream-volume-20260915.md)
addresses this rejected service. The recovered log establishes an intentional
worker abort for this run; the fragment-submission audit is a separate change.
