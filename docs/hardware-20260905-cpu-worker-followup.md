# September 5: hardware feedback after the texture-worker deployment

The installed combined build is documented in
[the texture-worker report](cpu-texture-worker-20260905.md). The user reports
8–15 fps depending on activity, dropping to about 4 fps while driving the Warthog,
and a frame drop when firing the assault rifle. These are user observations. USB logs, saves and recent screenshots were collected
at 20:55 CDT in `/home/birchwoodgod/xita-backups/2026-09-05-205510-cpu-worker-hardware/`.
The executable hash matches the deployed texture-worker build. All runtime/save
files copied; one older screenshot (`161238.png`) returned a device I/O error.
The five screenshots from 20:29–20:49 copied successfully and were inspected.

Sky rendering and decals look good on Vita, and active camouflage works. Geometry
spikes initially appeared absent in multiplayer and campaign, but the user later
corrected that report: spikes still occur, less often. Keep geometry stability open.

Other reported issues:

- Start opens a menu, but navigation can move the character instead of selecting
  Leave Game. The D-pad shortcut gate checks simulation pause; multiplayer needs
  the current UI root checked as well. The correction passed host and emulator
  testing and was included in the 22:06 USB deployment.
- The Enlisted Players screen remains visible while a match loads. The 20:29 screenshot shows the lobby; it does not establish
  whether its UI root survives entry into gameplay.
- Skipping a campaign cinematic does not return the camera to the player's body.
- Some glass renders completely black.
- The flashlight still breaks rendering. Rear touch is now disabled by default in
  the local candidate (`XV_REAR_TOUCH=0`); front touch remains available. This avoids
  accidental activation, but is not a flashlight rendering fix. This default was
  included in the 22:06 USB deployment.
- Custom variants retain their names but fail to apply vehicle options. The
  [variant investigation](variants-20260905.md) identifies the signature fallback.

## Performance next step

The user wants further CPU workload sharing. The current core-0 worker handles
large texture conversions only; low texture settings and cache hits leave little
work for it during many gameplay windows. Physics, AI and guest draw preparation
still share the ordered guest execution path. The existing render pump and audio
work already run separately.

Collect the installed build's function samples, frame/pump timers, conversion/join
totals and core/thread reports. Compare on-foot movement, Warthog driving and AR
fire in representative map windows. Function samples include waiting and cannot
be treated as hardware CPU-cycle measurements. Identify expensive, independent
per-frame work before splitting additional jobs; retain the fixed guest-index
lifetime and explicit completion boundaries. Sustained 20 fps remains the first
target, and has not been achieved in this run.

## Collected measurements

`xita.log` captures the campaign session through 885.833 seconds. In the final
three 60-frame windows (near 858, 870 and 880 seconds), throughput is 5.2–5.3 fps,
with 251–268 mesh draws/frame and 35.2–37.4 ms/frame in the draw HLE. All three
windows report zero texture decodes. After 800 seconds, average system-wide busy
time is C0 10.3%, C1 18.4%, C2 85.6%; individual C2 samples reach 96%.
The texture worker cannot shorten steady-state frames that contain no conversions.

The last 10,000-sample function profile reports DrawIndexedVertices at 18.6%,
0x11B5A0 at 8.5%, and 0x11B550 at 1.9%. The latter two are a generic sort and
its small-partition helper. One caller, 0x53FA0, sorts signed triangle numbers
with comparator 0x53F60 and emits three 16-bit indices for each triangle. Other
sort callers exist, so the aggregate sort samples cannot all be attributed to
world geometry. This is a candidate for native execution and bounded parallel
work; measure the specific caller before claiming a performance gain.

`xita.1.log` contains a 3.7 fps world window near 172 seconds, with only 160
draws/frame, 18.9 ms draw HLE and two texture decodes totaling 0.4 ms over the
window. It has not been correlated conclusively to the Warthog-driving interval.
Pump, guest and sampled-function times overlap and include waiting; do not add
them or describe the pump timer as CPU utilization.

The final campaign camera state retains scripted callback 0x120A90 and
`camera_control=1`. The zero byte at 0x2331D9 was initially described as
cinematic-active; subsequent inspection shows it is set by camera_control and
cleared during camera updates, including valid cinematic shots. It cannot prove
that a cinematic has finished. The user's stuck view still requires tracing the
skip path and camera handoff.
Existing unit position/forward diagnostic offsets require validation; their NaNs
are not sufficient evidence of corrupt object memory. The earlier heuristic
camera recovery remains disabled because it interrupted valid cinematic shots.

The 20:46 and 20:47 screenshots show stretched geometry and black window panels.
The 20:49 screenshot shows a clean view at another angle. Geometry stability and
black glass remain open.

## Additional emulator observations

The user confirms that both Scorpion and Ghost now work with the saved-variant
fix. Charging the plasma pistol makes the nearby Ghost disappear until the shot
is released. This may share the flashlight's dynamic-light failure; the cause is
not established. A black spot on the motion tracker appears while moving and
clears when stationary. Keep radar transparency and light-pass disappearance as
separate observations until state traces establish a connection.

Screenshots and requested frame traces are in `/tmp/xita-plasma-charge-20260905/`
and the variant lab's runtime log. Those captures caught radar/movement views,
not a conclusive before/charged/after view of the Ghost.
