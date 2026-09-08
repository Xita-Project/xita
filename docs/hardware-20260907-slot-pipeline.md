# Slot pipeline hardware result and 20 FPS goal — September 7, 2026

The physical Vita benchmark shows no improvement from allowing additional frames
in flight. The user subsequently reported a GPU driver crash while driving.
The active goal is sustained **20 FPS on the physical Vita**, preserving rendering
and gameplay at the user's standard settings. This goal is not achieved.

## Measured result

Evidence: `/home/birchwoodgod/xita-backups/2026-09-07-170330-slot-pipeline-hardware/`.
The collection contains the installed executable, configuration, logs, saves,
screenshots and crash dumps, with hashes for 79 files. The executable matches the
16:32 slot-pipeline candidate (`76687f93…`).

| Controlled comparison | Before | Changed phase | After |
| --- | ---: | ---: | ---: |
| Single-flight / triple / single-flight at 544p | 8.254 FPS | 8.097 FPS | 8.408 FPS |
| 544p / 360p / 544p | 8.381 FPS | 10.240 FPS | 8.467 FPS |

Each phase measures 120 frames. All six phases report matching camera position
and orientation; the resolution test restores 544p. Triple submission is about
2.8% below pooled single-flight throughput in this run. At 360p throughput is
about 21.6% higher than the pooled 544p controls. Neither reaches 20 FPS.

The last full gameplay window, mesh frames 2880–2939, reports about 6.1 FPS:

- Pump submission 4.019 ms/frame, display queue 0.030 ms/frame, normal Finish
  calls **0/0/0**. Moving the wait did not remove the workload.
- 153.93 actual GXM draws/frame on average, range 103–238. This window does not
  support a blanket 500–800 draw-call sorting diagnosis.
- Vertex stream preparation 5.922 ms/frame. Snapshots copy 22,570 KiB and compare
  31,055 KiB over 60 frames, with zero upload failures and 481 KiB maximum slot
  usage. This is a concrete CPU optimization target; it is not an argument to
  return to mutable cached guest pointers.
- Notification completion latency 113.763 ms/frame in that window. Earlier
  driving windows reach 198.848 ms with two pending frames. This latency overlaps
  guest and submission work and is **not exclusive GPU execution time**.

Both CPU preparation and GPU work need attention. Resolution scaling helps this
view but does not remove the remaining frame cost. Cross-build FPS or upload-cost
comparisons from different camera positions are not controlled measurements.

## Crash and recovery

`psp2core-1788818414-GPUCRASH.psp2dmp`, timestamp 17:00, decodes successfully.
Its TTY record ends with `appmgr_aborthandler.c(793) render gpu crash`.
The sampled pump CPU thread is sleeping in `sceKernelDelayThread`; the dump does
not establish a CPU data abort. GPU register data is archived, but the dump does
not include the frame-packet BSS needed to reconstruct slot ownership. The exact
faulting GPU resource and root cause remain unresolved. An earlier GPU crash
predates this pipeline change, so the queue is not a proven universal cause.

Recovery defaults to **single-flight submission**, retaining three owned buffer
slots, immutable uncached uploads, and fragment-notification retirement. Explicit
diagnostic comparison can still opt into triple submission. Host acquisition and
completion tests pass, including the restored default and ticket wrap.

Private Vita3K renders the dashboard, main menu and Blood Gulch, including camera
movement, firing and flashlight input. Three sampled geometry captures find zero
changed draws before completion. Logs show no normal Finish calls, fence errors,
upload shortages or storage drops; maximum pending count is one. The emulator was
stopped and its previous private executable/configuration restored. This is a
regression check, not hardware crash clearance or a Vita FPS measurement.

The user cancelled the new-VPK request. The recovery executable was written into
the current **30,891,526-byte** allocation over USB, direct-read verified, verified
again on a fresh read-only mount, and safely unmounted at 18:06 CDT. Settings,
saves and 1,654 other checked files stayed unchanged. The actual intervening
Claude-installed executable (`6f97c929…`) was backed up first. The recovery uses
the preserved, tested generated-code staging baseline; root generated files
differ from it and must not be silently substituted into a rebuild.

Installed SHA-256:
`8fbb6ec4b7bf0a01cfafea3146b2d7bb17e6caace47f8de62d4c7141f2e99e83`.
Candidate, full staged source and deployment evidence:
`/home/birchwoodgod/xita-backups/2026-09-07-170945-single-flight-vpk/`.
The directory name is historical; deployment used the executable, not a VPK.

## Reviewed experiments and next work

Claude's unfinished visibility and precision work is preserved separately. It is
not in the recovery executable. Their defaults in the working source now retain
exact visibility completion and full-float generation.

The stale-result helper permits arbitrarily old results. Halo's inspected
`0x60460` consumer uses query-list indices, converts returned pixels into a
0–255 coverage ratio, and smooths a flare intensity. Reusing a numeric ID does not
prove that the previous count belongs to the same flare or screen rectangle.
Before enabling this experiment, track stable identity, frame age and viewport
changes, and validate moving/occluded light sources. Current HLE/publication tests
pass in five modes, including threaded handoffs; they do not validate the opt-in
stale approximation.

The broad half conversion also lowers output-alpha arithmetic, so retaining a
float comparison alone does not preserve alpha-test decisions. A separate probe
compiled 12 full/half pairs for the common `154066FD` family while keeping sampled
tex0 alpha and output alpha at full precision. All 24 compile successfully.
GXP sizes fall by 0.4–3.9%; this is not an execution-time measurement or proof of
visual equivalence. Sources, compiler outputs and analysis are archived in
`/home/birchwoodgod/xita-backups/2026-09-07-175145-half-material-probe/`.

Next priorities:

1. Check the recovery build during a short driving route and preserve any new
   crash evidence. An identical slot benchmark is unnecessary.
2. Reduce the measured snapshot-copy/compare cost while preserving recorded
   bytes and GPU lifetime. Consider bounded worker copies or safe reuse; verify
   mutations, exhaustion and publication ordering before hardware comparison.
3. Finish a separate material-shader comparison. Keep dedicated cutout and
   precision changes separate so any benefit or visual regression is attributable.
4. Investigate identity-aware asynchronous flare results as a later comparison.
   Never present an older result as exact completion or assume every wait can
   disappear without changing frame dependencies.

Each hardware result must say what improved, what did not, and what is next.
Emulator FPS, smaller shaders and removed API waits do not satisfy the 20 FPS
acceptance gate in the roadmap.
