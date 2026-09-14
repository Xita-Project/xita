# Sharing large vertex snapshots

The physical cryo-room diagnostic measures about 7 ms/frame creating initial
vertex snapshots after indexed validation. The existing core-0 worker copies
those snapshots into GPU memory; the game thread still creates them.

The new experimental `XV_SNAPSHOT_WORKER=1` path shares cached snapshot copies of
at least 128 KiB between the recording thread and core 0. It is off by default.
It requires the existing vertex upload worker. Only an idle queue accepts this
work; busy queues and thread/semaphore failures leave the existing caller copy
in place without changing destination bytes.

The source is borrowed synchronously. The owner copies one half while core 0
copies the other, and joins the completion ticket before returning to guest
execution. Both writers split at an absolute 64-byte boundary. Only after that
join can the completed owned snapshot be queued for the existing asynchronous
GPU upload. No guest pointer survives the synchronous call. Source and
destination must be disjoint and remain valid throughout it. Draw ordering,
index validation, GPU retirement, shaders and allocation sizes are unchanged.

This trades less owner copy work for a dispatch and join. The initial version
used the existing 100 µs completion polling; shared memory bandwidth and wakeup
costs may erase the benefit. The
`snapshot-worker` remote benchmark selects only this experiment at the current
resolution, refuses when the underlying upload worker is disabled, and restores
the configured mode on completion, cancellation or loss of first-person control.
The `[snapshot-worker]` record counts core-0 halves, bytes and elapsed work time;
it is separate from GPU-copy work. Both can overlap other timing categories.

Validation uses the production worker and uploader with real pthreads: startup
and signal failures, busy queues, delayed completion, 64 destination alignments,
odd sizes, surrounding canaries, immediate source changes after return, and 81
retained slot generations. Existing delayed-copy and 300-generation checks also
pass. ASan/UBSan and ThreadSanitizer runs pass. The benchmark admission and
restoration tests and real HTTP remote suite pass. The production frame
acquisition fixture also verifies all 17 experiment branches select only their
intended override across configuration modes and ticket wrap.

The first ordinary candidate runtime is
`7f33dee4f5df113b7d62a00b195acc859dd36ebf84a2a17f85c1ca7c9f514946`.
It uses the existing 1,588-entry updater contract and changes only the game
executable and boot manifest. Vita3K renders the campaign and completes the off/on/off comparison with
matching camera checks, nonzero core-0 snapshot work, no recorded vertex upload
failures and restoration to the configured disabled mode. Emulator FPS is not a
physical performance result. The physical updater has confirmed this exact
candidate in slot A. The initial physical results are below.

The emulator again crashed inside its in-process app restart, after Xita drained
the GPU/display and handed off to the helper. Restarting the isolated emulator
completed the staged installation and verified the candidate hash. This is a
repeatable emulator restart issue, separate from the completed campaign trial.


## Initial physical result: retain the disabled default

The first three off/on/off comparisons at 640 × 360 in the cryo room all pass
the camera check, exercise the shared work and report zero vertex-upload
failures. Each arm has 60 settling frames followed by 120 measured frames.
Indexed vertex checks and the existing GPU-copy worker stay enabled throughout;
all other settings stay fixed. Requested CPU 500 MHz reads back as 444 MHz;
GPU/bus/crossbar read back as 222/222/166 MHz. The cap is 30 FPS.

| Trial | Off before | Shared snapshots | Off after | Saved ms/frame |
| --- | ---: | ---: | ---: | ---: |
| 1 | 6.809 FPS | 6.733 FPS | 6.678 FPS | -0.216 |
| 2 | 6.789 FPS | 6.673 FPS | 6.755 FPS | -2.190 |
| 3 | 6.750 FPS | 6.722 FPS | 6.723 FPS | -0.324 |

Pooling exact elapsed times gives **6.750 FPS off versus 6.709 FPS on**, or
**0.910 ms/frame slower** with sharing. Core 0 really executes the additional
copies, but higher utilization does not establish a gain. The measured windows
show extra completion waits and no clear stream-preparation reduction. This is
not a reason to enable the experiment in ordinary gameplay. One additional
turned cryo-room view measures 7.781 FPS off versus 7.714 FPS on, or 1.116
ms/frame slower; its camera and upload checks also pass. The benchmark
restores the configured disabled mode.

## Completion notification follow-up

A second version replaces only the synchronous source-loan polling loop with
a dedicated event flag. The recording owner is its sole waiter. Completion
still requires an acquire-load of the exact ticket; a notification alone never
permits returning borrowed memory. Stale events only cause a recheck. A bounded
1 ms event wait and a ticket recheck recover from a lost firmware notification;
failed waits back off rather than spinning. Creation failure leaves the caller
copy unchanged. The ordinary asynchronous GPU-copy consumer retains its prior
wait and all GPU slot-retirement rules remain unchanged.

Additional real-thread tests inject event creation, signal and wait failures,
deliberately stale notifications, and delayed consumer completion. ASan/UBSan
and ThreadSanitizer pass. This follow-up still requires its own emulator and
hardware measurements; the polling result above must not be attributed to it.

The completion-notification candidate runtime is
`0c58ac29dd9cf5f06ab3aab37d68717d9a8c96dbe37726d73dae2f099fd10c16`.
Its package preserves the same updater helper, contract and asset set.
