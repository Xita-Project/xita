# Indexed vertex comparison installed — September 8, 2026

The primary Vita now has the indexed vertex validation candidate from source
`fe572b7`, installed over USB at approximately 22:44 CDT. **No hardware gameplay
or benchmark has been collected after this update.** Sustained 20 FPS and the
earlier hardware rocket/death crash remain unverified.

The [experiment and local results](vertex-references-20260908.md) describe the
tradeoff: less vertex data compared, with additional index-mask construction.
The campaign emulator sample reduced the combined preparation time, while a
light Blood Gulch view showed a small increase. Those samples do not predict a
Vita FPS gain.

## Installation evidence

- Backed up the existing executable, settings and logs over read-only USB.
  All 1,584 non-executable app payloads matched the candidate's baseline package.
- Updated the existing executable and configuration files without creating,
  truncating, renaming or changing their sizes: 34,985,878 and 531 bytes.
- The executable is a VitaSDK compressed signed SELF padded to the existing
  size. Its three decoded segments match the native executable checked in Blood
  Gulch; this exact padded file also loaded campaign and completed its comparison
  in the private emulator.
- Verified both writes with direct device reads, then again after a fresh
  read-only mount. Checked 1,587 other recorded files unchanged and safely
  unmounted the Vita.
- Graphics settings are unchanged. The diagnostic selector now chooses vertex
  validation, and the experiment remains disabled outside its benchmark phases.
  The separate ad hoc tester was not replaced by this Halo update.

Installed `eboot.bin` SHA-256:

```text
8b77fac09c4c2c5b91f14d748c7e0d12a0dbe48bc530813b6ec48220fff2349c
```

The private development release's `xita-vertex-references-20260908.vpk` contains
that same executable and the same 1,584 other payloads. It is a recovery/install
package; the connected primary Vita already received the executable directly.
It contains no user configuration, saves, game image or maps.

## Next test

Load Blood Gulch, face buildings or terrain, and stop moving. Press **L + R +
Square** and wait for the three phases to finish. The test holds resolution and
graphics settings fixed, checks the camera and restores the configured setting.
If practical, repeat from a stationary campaign view before reconnecting USB.

Compare complete frame times and the `[draw-prep]` index/stream timings alongside
the `[vertex-references]` counters. Keep the experiment disabled if hardware
shows no useful gain. Driving and rocket/death correctness with it enabled are
separate checks; the benchmark's stationary camera does not cover them.
