# Developer testing

[README](../README.md) · [Roadmap](../ROADMAP.md) · [Release audit](release-audit.md)

For unattended hardware work, see [remote testing](remote-testing.md): optional
LAN screenshots, controller input, log collection and repeated benchmarks.

## Sending tester logs

Normal campaign play is useful; a benchmark is not required to report a bug.
After playing, close Xita and open VitaShell USB mode. Copy these files from
`ux0:data/xita/` (shown as `data/xita/` on the computer's Vita drive):

| File | Contents |
| --- | --- |
| `xita.log` | Current run: startup, rendering diagnostics and benchmark results. |
| `xita.1.log`, `xita.2.log`, `xita.3.log` | Previous runs, if present. Include these if Xita has been relaunched since the issue. |
| `xita.cfg` | Saved settings used to interpret the run. |
| `adhoc.log` | Separate ad hoc tester output; include for networking tests. |

Zip the copied files and send them privately to the maintainer. Include the
installed VPK filename, map/mission, what happened and approximate time into the
session, plus a screenshot if available. Copy logs before relaunching Xita:
each launch rotates the previous run, retaining three older logs. The game
image, maps and saves are not needed for this initial report.

## Diagnostic settings

Settings live in `ux0:data/xita/xita.cfg`, one `KEY=VALUE` per line. In Vita3K,
`env.txt` supplies fallback values; the configuration file takes precedence.
The dashboard updates only the edited setting and preserves unrelated keys and
comments. Restart to apply launch-time changes.

| Setting | Purpose |
| --- | --- |
| `XV_TRIPLE_BUFFER=0/1` | Experimental frame overlap. Default 0 keeps one published frame in flight; 1 permits overlap using three owned slots. GPU completion is required before reuse in either mode. |
| `XV_FLARE_DEFER=0/1` | Exact visibility results read at their next dependency. The current local candidate defaults on. Zero restores eager reads; stale visibility mode remains incompatible. |
| `XV_FRAME_CONSTANTS=0/1` | Frame-owned mesh shader constants. Default on in the crash candidate. Turning it off is diagnostic, not a recommended graphics setting. |
| `XV_RENDER_HEIGHT=360/400/480/544` | Game render resolution, upscaled to the Vita display. |
| `XV_FPS=1`, `XV_CPU=1` | Frame overlay and sampled per-core busy counters. Counters include system work; they are not GPU utilization. |
| `XV_THREADS=1` | Thread and affinity diagnostics. |
| `XV_VERTEX_WORKER=0/1` | Experimental core-0 copies from immutable vertex snapshots. Default off; dashboard Performance switch, relaunch required. |
| `XV_DASHBOARD=0` | Bypass the dashboard for automation. |
| `XV_SHADER_OVERRIDE=1` | Prefer device shader files over packaged ones; development only. |
| `XV_PROF=1` | Enable sampling; regenerate with `--trace-funcs` for guest attribution. Instrumentation changes the workload. |

**L + R + Square** uses the enabled benchmark selector. With no more specific
selector enabled, it runs eager/deferred/eager visibility comparison. Check the
`[...-compare] start` line and the build notes before interpreting results.
The [native math selectors](native-math-benchmark-20260913.md) take precedence
over older [indexed validation](vertex-references-20260908.md) and worker tests.
The comparison preserves configured buffering, shader quality, resolution and
the frame cap, then restores the configured experiment setting.
Use a loaded first-person view, hold still, and keep settings unchanged.
**L + R + Select** compares 544p/360p/544p instead. Either shortcut cancels an
active comparison; completion/cancellation restores its original settings.

## Overlay units

The experimental [parallel vertex upload comparison](vertex-upload-worker-20260909.md)
uses `XV_BENCHMARK_VERTEX_WORKER=1` and **L + R + Square**. Native math selectors
must be off to select this comparison. Other settings are preserved.
The dashboard Performance switch requires relaunching Xita. It defaults off
until measured on hardware.

| Display | Meaning |
| --- | --- |
| Green / left | FPS derived from 60-frame timing windows. |
| Amber / middle | Milliseconds outside the Present path: guest work, HLE, draw preparation and associated waits. |
| Blue / right | Milliseconds in the Present path: frame finalization, publication, slot acquisition and conditional maintenance. **Not GPU MHz, utilization or total GPU execution time.** |
| C0 / C1 / C2 | System-wide CPU busy percentages over roughly one second; dashes mean unavailable. |

The GPU runs asynchronously. A low blue number does not establish that it is
idle. Effective clock frequencies are recorded separately in the startup log.
See the [tester feedback review](tester-feedback-20260909.md) for current
synchronization and worker behavior.

Sixty-frame FPS windows do not measure instantaneous peaks or 1% lows.
Visibility wait and notification-latency timers overlap other work; do not
sum them to reconstruct CPU/GPU execution time. Repeated diagnostic camera
rows alone do not establish that a user held still.

For emulator helpers, see `tools/vita3k.sh`. Keep a separate lab copy of game
data and settings. A good emulator result does not clear physical GPU crashes.
Keep raw logs and dumps private; release diagnostics only after review.
