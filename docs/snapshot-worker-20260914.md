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

This trades less owner copy work for a dispatch and join. The existing 100 µs
completion polling and shared memory bandwidth may erase the benefit. The
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
candidate in slot A; physical off/on/off timing remains pending.

The emulator again crashed inside its in-process app restart, after Xita drained
the GPU/display and handed off to the helper. Restarting the isolated emulator
completed the staged installation and verified the candidate hash. This is a
repeatable emulator restart issue, separate from the completed campaign trial.
