# First remote Vita session — September 13, 2026

Authenticated Wi-Fi input launched Halo from the Xita dashboard on the physical
Vita. Screenshots guided normal solo multiplayer menu navigation into Blood
Gulch; remote stick input turned the camera. The host downloaded the log and
completed three native object-basis off/on/off trials without local input.
All camera checks passed and the configured helper was restored after each run.

| Trial | Off before FPS | On FPS | Off after FPS | Saved frame time versus pooled off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 11.447 | 11.708 | 11.849 | 0.462 ms |
| 2 | 11.916 | 11.768 | 11.845 | -0.802 ms |
| 3 | 11.823 | 11.834 | 11.620 | 0.821 ms |

Pooling exact elapsed microseconds gives **11.748 FPS off / 11.770 FPS on**,
or 85.123 / 84.963 ms per frame. The 0.160 ms aggregate difference (0.188%)
changes sign between trials and does not establish a useful speedup. There are
720 off and 360 on measured frames; each phase excludes 60 settling frames.
This is one fixed view with live simulation, not moving gameplay or a stability
soak. Object-basis counters confirm accepted native work during the enabled arm.

Settings remained at 360p, 20 FPS cap, triple buffering and vertex worker enabled,
model-palette disabled, detailed phase timing disabled. The 500 MHz CPU request
failed; the clock API reports 444 MHz CPU / 222 MHz GPU. The remote service and
1 Hz status polling remained enabled in every arm; their overhead was not
isolated. Screenshots and full log downloads occurred outside the timed arms.

Reports overlapping the trials show zero busy-slot acquisition waits. Median
C0/C1/C2 percentages by trial were 4/13.5/89.5, 4/10/92 and 4/9/92. These are
system-wide samples, not timings of individual engine jobs. GPU completion
latency overlaps guest/submission work and must not be read as CPU blocking time.
Larger scene/object preparation work remains the next performance target; the
separate model-palette experiment still needs its own controlled comparison.

The running remote service reports build `Sep 13 2026 22:06:30`, matching the
verified transferred candidate (`6eff881…` runtime, `80bcdb2…` VPK). This confirms
the remote-enabled build is running, without claiming a fresh installed-file
hash read over Wi-Fi. Private evidence, before/after screens, exact phase times
and SHA-256 receipts are under
`2026-09-13-worker-sizing/validation/remote-test/hardware-session-20260914T041222Z`.
Controls were released and the keep-awake lease cleared after testing.

Remote installation is not part of this tested build. An integrated updater is
the next development task; first installation and full-system crash recovery
still require local assistance.
