# Developer testing

[README](../README.md) · [Roadmap](../ROADMAP.md) · [Release audit](release-audit.md)

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
| `XV_DASHBOARD=0` | Bypass the dashboard for automation. |
| `XV_SHADER_OVERRIDE=1` | Prefer device shader files over packaged ones; development only. |
| `XV_PROF=1` | Enable sampling; regenerate with `--trace-funcs` for guest attribution. Instrumentation changes the workload. |

In the current local candidate, **L + R + Square** runs eager/deferred/eager
visibility comparison. It preserves configured buffering, shader quality,
resolution and the frame cap, then restores the configured visibility mode.
Use a loaded first-person view, hold still, and keep settings unchanged.
**L + R + Select** compares 544p/360p/544p instead. Either shortcut cancels an
active comparison; completion/cancellation restores its original settings.

Sixty-frame FPS windows do not measure instantaneous peaks or 1% lows.
Visibility wait and notification-latency timers overlap other work; do not
sum them to reconstruct CPU/GPU execution time. Repeated diagnostic camera
rows alone do not establish that a user held still.

For emulator helpers, see `tools/vita3k.sh`. Keep a separate lab copy of game
data and settings. A good emulator result does not clear physical GPU crashes.
Keep raw logs and dumps private; release diagnostics only after review.
