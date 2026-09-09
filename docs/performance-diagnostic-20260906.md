# September 6 performance diagnostic build

**Historical baseline:** the Finish timings below precede
[asynchronous slot retirement](slot-pipeline-20260907.md). Ordinary frames in
the current renderer no longer call `sceGxmFinish`. See the
[September 9 review](tester-feedback-20260909.md) for current measurement limits.

The latest ordinary Vita build records Blood Gulch at a median 8.1 FPS, with
9.45 ms in the indexed draw adapter against a 118.35 ms game-side interval.
Its 79.05 ms render-pump timer combines submission and waiting. This diagnostic
build adds attribution for the remaining guest work and separates pump stages.
It is a measurement build; no FPS improvement is claimed.

## Measurements

The isolated recompile enables the existing guest sampler automatically. It
instruments 8,021 function entries and 35,402 direct-call returns. Removing
those markers reproduces all 32 current generated source files byte for byte,
including the solo Split Screen patch. Dispatch tables and prototypes were
also compared, allowing only the guest-tracing build marker to change.
The ordinary generated files in the working checkout remain uninstrumented.

`[prof]` reports the top guest functions and native HLE targets every 10,000
samples, requesting a 1 ms sleep between samples. These are sampled wall-time
shares, including preemption and waits, rather than exclusive CPU-cycle costs.
The existing wait report accompanies each sample window. `XV_PROF=0` disables
the sampling thread; it does not remove the compiled function markers.

`[render-time]` reports mutually exclusive elapsed stages every 60 pump frames:

| Stage | Includes |
| --- | --- |
| `submit` | Scene setup, command replay, uniforms/state, UI, upscale and other pump work outside the named stages. GXM calls can stall internally. |
| `previous-finish` | Existing completion waits before previous-frame sampling or render-target replay. |
| `target-finish` | Existing completion waits between render targets, including error cleanup. |
| `frame-finish` | Final completion wait protecting frame-owned vertex, index and UI storage. |
| `display-queue` | Elapsed time inside the display-queue submission call. |
| `retire` | Completed visibility-query collection, geometry checks and frame-completion publication. |

The report includes mesh-frame identifiers and the number of finish/queue calls.
The stages sum to the measured pump total before decimal rounding. They exclude
idle polling, the optional frame-cap delay, CPU-usage polling before the frame,
and the report's own file write. Reports are written after publishing completion.
All timing accumulators and resets belong to the pump thread.

The new timer defaults on in traced builds and off in ordinary builds.
`XV_RENDER_PROFILE=1` or `0` explicitly overrides that choice. Calls outside a
measured pump frame are ignored. The existing draw-preparation timers and worker
reports remain available. GPU waits, submission and guest intervals must not be
added as independent costs across threads; none of these timers measures GPU
shader execution directly.

No existing GPU completion calls, frame-buffer ownership, shaders, gameplay
clock or CPU-worker assignments were changed for this diagnostic.

## Validation

- Host tests cover disjoint stage accounting, repeated target waits, idle-time
  exclusion, frame-window reset, a zero clock origin, ordinary/traced defaults,
  explicit overrides and an unavailable clock.
- Existing sampler and indirect-dispatch tests pass, including nested HLE
  callbacks and restored caller attribution.
- Production pump and render-target lifecycle tests pass with the added timing
  calls, preserving GPU completion before buffer reuse and effect consumption.
- AddressSanitizer and UndefinedBehaviorSanitizer pass for the new timer and
  indirect-dispatch attribution tests.

- Native `make RECOMP=1 -j6` succeeds in the isolated source snapshot. An omitted
  `.inc` dependency in the initial staging copy was restored before the final
  successful build; both build logs are preserved.
- The exact same-size USB SELF boots in an isolated Vita3K OpenGL/llvmpipe
  session on Xvfb. Ordinary Campaign menu selections resume the copied combat
  checkpoint. Camera turns and plasma firing work. This software-rendered
  emulator run is a functionality check, not hardware performance evidence.
- The run produces 28 function-sampling windows, 138 render-stage windows and
  100 world-rendering timing windows. All 138 stage totals agree within printed
  rounding, with the expected final-finish and display-queue call counts.
  Repeated target-finish calls are exercised in gameplay. All 139 recorded
  geometry-capacity windows have zero command/index/attribute/immediate drops.

## Prepared candidate

Archive:
`/home/birchwoodgod/xita-backups/2026-09-06-084150-performance-diagnostic/`.
It contains the full source snapshot, source-equivalence hashes, ELF/VELF,
native and compressed executables, VPK, same-size USB executable, logs and
screenshots. The user's running emulator session was left in place; the
separate diagnostic session has been stopped.

The traced native SELF is 34,099,478 bytes. VitaSDK's standard SELF compression
reduces it to 11,572,370 bytes so it fits the existing 32,918,474-byte device
allocation. The USB copy pads that compressed SELF to the existing length and
updates its container-size field. All three decoded segments are byte-identical
between native, compressed and padded forms. The VPK contains the verified
compressed executable.

- Native SHA-256: `b98b3c5f4bfbc648c2e95edf3f12e9e00c1b9cef9e9031c1d9594a90cefc33ea`.
- Compressed SHA-256: `1b0a643c801c0a06782d657c8b2e7cd6385f46a421d35feb7c4980aa91c7b734`.
- Prepared USB SHA-256: `6159534a2870ea94347284f2eb3e187f08310f46eaca6f1d1e148d2f38cea683`.

Installed over USB on September 6 at 08:54 CDT. Direct readback and a separate
read-only remount verification at 08:55 both match the prepared USB SHA-256.
All 657 checked other files, including configuration and saves, are unchanged
after remounting. The card was safely unmounted afterward. No VPK installation
is needed; the existing executable allocation was updated in place.

The 65-file pre-install backup is
`/home/birchwoodgod/xita-backups/2026-09-06-085208-before-performance-diagnostic-usb/`.
It retains the previous `7067df1e...` executable, logs, configuration, saves and
today's screenshots. The device still uses 480p, textures 128, mip smoothing
off, filtering 1 and rear touch off. Hardware profiling results were collected
after the user's next Blood Gulch run; see below.

## Hardware results collected at 09:04 CDT

The read-only USB backup is
`/home/birchwoodgod/xita-backups/2026-09-06-090409-performance-diagnostic-hardware/`.
The executable still matches `6159534a...`; configuration is unchanged. The
553,900-byte log contains 48 matched frame/render windows, including 30 Blood
Gulch world windows and 21 function-profile tables bracketed by world windows.
Parsed records and summary statistics are in the backup's `analysis.json`.

| Measurement | Median across world windows |
| --- | ---: |
| FPS | 6.5 (range 4.9–17.5) |
| Guest frame interval | 152.45 ms |
| Indexed draw HLE | 9.70 ms |
| Pump submission | 3.742 ms |
| Previous-frame completion wait | 0.006 ms |
| Between-target completion waits | 85.205 ms |
| Final-frame completion wait | 2.672 ms |
| Display-queue call | 0.033 ms |
| Retirement | 0.047 ms |
| Whole measured pump | 91.340 ms |

The 60-frame window at log line 3771 averages 17.5 FPS with 49 draws per frame;
its camera sample points almost straight down (`forward.z = -1.00`). This fits
the user's observation of roughly 20 instantaneous FPS looking at the ground.
The world median is 168.5 draws per frame. Camera direction is sampled at the
end of a window, so it does not describe every frame within that window.

Between-target waits dominate measured pump time. This is time blocked on GPU
completion, including real rendering work; it is not 85 ms of removable CPU
overhead. The next bounded experiment queues consecutive scenes on the same
GXM context while retaining final completion before frame storage reuse. A
successful change may move waits into later GXM calls rather than remove them;
compare whole frame and pump time, not just the target-wait counter.

Function sampling also identifies visibility-result polling (mean printed
share 10.47%), indexed draw HLE (6.15%), Present (6.11%) and guest function
`0x88B80` (5.63%, peak 13.1%). The latter recursively traverses BSP nodes and
splits intervals against planes; its precise gameplay callers need further
attribution before choosing a native replacement. Shares are sampled wall time,
and functions outside a table's top 40 are counted as zero for these means.
These are not exclusive CPU costs. Nearby core samples have medians C0 13.5%,
C1 21%, C2 73%; moving more guest work to workers alone cannot eliminate GPU
completion waits.

This run uses instrumentation and a different route from the earlier ordinary
build. Its median must not be treated as an A/B regression. All matched render
stage sums agree within printed rounding. Draw-capacity logging was not enabled
on the device, so this run does not establish a zero-drop result.

## Hardware test after installation

Use the same 480p / textures-128 settings. In Blood Gulch, spend approximately
30 seconds looking around on foot, 30 seconds firing, and 30–60 seconds driving
the Warthog. Reconnect in VitaShell USB mode afterward. A campaign combat run
can follow separately so its AI/physics workload is distinguishable.

Keep diagnostic options consistent between compared runs. Instrumentation adds
work, so any optimization selected from these samples still needs an ordinary
build check on the same hardware route. The proposed automatic benchmark
dashboard action remains separate work.
