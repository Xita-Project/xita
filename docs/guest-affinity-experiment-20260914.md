# Presenting-thread affinity experiment — September 14

The corrected CPU diagnostics accept named thread records whose affinity mask is
zero, the SDK's default value. Physical startup logs now expose actual thread
names, sampled core IDs, runtime counters and migration counts. Previously those
records were incorrectly labeled unavailable.

The render pump reports core 1 with mask `0x00020000`. Guest fibers report the
default mask and can migrate between application cores. For example, the same
presenting thread was sampled on cores 0, 1 and 2 during startup. This observation
does not imply that multiple guest fibers execute concurrently: their semaphore
handoff still permits only one at a time.

## Controlled comparison

`XV_GUEST_AFFINITY=1` adds the remote `guest-affinity` benchmark. It preserves the
current presenting thread's exact original mask and compares original/core 2/
original, at the current resolution. The command runs on the ordinary guest
owner after queued frames drain. No worker affinity, thread priority, cooperative
handoff or game update order changes. There is no ordinary-play affinity change.

The runtime checks setter results and reads back the applied mask. A changed
presenting thread or failed API cancels the comparison. Restoration addresses the
original thread even if the caller changed, and retains its ID for retry if
restoration fails. The remote client rejects fresh affinity-failure records,
including failures after measured phases, instead of accepting their FPS output.

Host checks cover original masks including zero, core-specific and all-user-core
masks; untouched workers; read/set/ignored-set failures; changed ownership;
restoration and retry. ASan/UBSan pass. Benchmark tests exercise completion,
cancellation and builds without the optional helper. The production frame
acquisition fixture and real HTTP selector/failure handling pass.

## Hardware context and limits

The Pillar of Autumn cryo-room capture has roughly 430 draws per frame and about
35 ms/frame in vertex-stream preparation. One 60-frame window copies about
94 MiB into retained snapshots and compares about 214 MiB. Its existing core-0
copy worker already processes the cached snapshots; exact guest-memory comparisons
and the initial snapshot copy remain on the guest thread. Moving those reads to
a worker requires an immutable input lifetime before guest execution can resume.
The guest source address alone cannot establish that lifetime.

This makes affinity a bounded hypothesis, not a claim that migration accounts for
most of the slowdown or that pinning a thread creates additional parallel work.
Private build, emulator and physical evidence are stored under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z/guest-affinity`.

## Emulator integration

The compatible package booted through the updater and entered ordinary solo
Blood Gulch. Three comparisons completed around the existing 20 FPS cap. Logs
show the original `0x00070000` mask, applied `0x00040000` core-2 mask, and exact
restoration after every trial, without affinity failures. Vita3K does not model
physical CPU scheduling performance; this verifies the test's controls only.

The test executable SHA-256 is
`8cdcd52a15e63412795e0c67d0392b81e2b5e624203cc4a8d9ffb187ffaeef39`.
Its updater contract and immutable helper/assets match the previously verified
package. Only the game executable and boot manifest change.
