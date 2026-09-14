# Halo 2: fixed spatial FXIN2 processing

The later [seven-source loop checkpoint](halo2-fxin2-loop.md) completes the
original bin23..25 pairs. This document preserves native117 evidence.

Native117 executes the original spatial bin-23 buffer creation, deferred
minimum/maximum distance setters and Play, then reaches the next nonspatial
bin-24 descriptor. Three actual sources feed the GP and emulated Vita sink.
The game still displays its first black frame and supplies zero nonzero grains
at this point. **No main menu or gameplay is demonstrated.** The original
visible Microsoft intro remains [native106](halo2-visible-intro.md).

This processing requires the explicit `AUDIO_SPATIAL_MODEL=1` option, which
requires `AUDIO_DSP=1` and defaults to zero. It implements the one observed
symmetric, zero-position case using the [pinned emulator filter
model](halo2-hrtf-model-provenance.md). Its normalization and smoothing are not
proof of MCPX transition timing. It neither bypasses the filter nor invents
silent completion.

## Original executable evidence

The owned XBE SHA-256 remains
`03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d`.
The descriptor is size 24, flags `100010`, zero external bytes, null format,
null mix-bin list, input bin 23 and no aggregation. Original return addresses
are `21E88A` for creation, `21E89D`/`21E8AC` for deferred FLT_MAX maximum/minimum
distance, and `21E8BA` for Play with three zero arguments.

A private Unicorn audit executes 2,559 unique original instruction addresses:
parameter/voice construction, real voice-pool bookkeeping, setup, both public
setters and public/common/voice Play. Only the allocator and command FIFO
capacity are isolated. The audit captures original hardware command writes;
it does not execute an APU or produce sound.

The original settings select mono 48 kHz signed-24-in-32, five unity routes
`6,8,7,9,10`, zero voice headroom and a 32-sample looping FX source. The hardware
HRTF submix map is `6,8,7,9`, and initialization sets HRTF headroom zero.
Original Play calculates unity attenuation for routes 6, 7 and 10, with routes
8 and 9 muted; pitch, envelope selection and auxiliary filter mode remain zero.
Listener pending position is zero, orientation is +X/+Y, distance factor is
`4043126F`, rolloff/Doppler factors are zero and dirty flags are `25`.
The adapter checks the exact observed state before activation.

The original filter upload selects equal left/right 31-tap sets at `386958`
and `386978`, followed by zero interaural delay. Each 32-byte view has SHA-256
`cf312104f620e3b4d8fd15fb1025c850fddec51e2c69296953858c580ca4f5bf`.
Runtime reads the owned views, checks mapping, equality, zero delay, selected
mode and the recorded fingerprint before creating a resource. Output and
allocation aliases of the filter/control inputs are rejected, including
physical aliases through another guest mapping. No coefficients are tracked.

## Mixer, lifetime and strict limits

The spatial buffer is a distinct owner of the same prior completed FX23 frame
used by the nonspatial buffer. It has a separate active bit, frame counts,
filter history and source-tagged sink progress. Creation retains the device
and checked DSP source; Play waits for a grain containing that source. Existing
prepared grains cannot satisfy the new Play. Inactive Release retires only its
own binding, while terminal close drains and joins the worker before freeing
the engine.

The symmetric filter processes the source and feeds actual GP bins 6, 7 and 10.
These are separate from the existing two sources' bins 0 through 5. Bins 8 and
9 remain muted according to the original attenuation calculation. Every source
reads before one shared GP frame executes; no additional GP time is fabricated.
The model restores the caller's full floating-point environment.

Dynamic spatial changes, other positions/filter shapes/delays, active Release
or Stop, arbitrary gains/routes, bins 24/25 and PCM playback with loaded effects
remain strict boundaries. Halo CE files and default execution are unchanged.

## Validation and native observations

All 40 host executables and extra channel modes pass, as do 27 focused Python
tests. Synthetic tests cover the one-tap smoothing recurrence, signed input,
filter history across frames, FP controls/status restoration, separate GP bins,
source ownership and concurrent retained-grain attribution. An additional run
of the tracked ABI test with a privately supplied owned filter covers exact
creation/setter/Play ABI, aliases, failure paths and lifetime; the ordinary
asset-free run verifies rejection of an invalid filter. ABI and concurrent
worker ASan/UBSan pass. The worker TSan concurrency subset passes, retaining
[the documented injected-longjmp exclusion](halo2-fx23-mixing.md).

A comparison against the actual pinned xemu filter passes 1,052,672 samples
across 257 cases (one owned coefficient set and 256 synthetic sets). The same
comparison and FP/history tests also pass as compiled Cortex-A9 code in the
isolated Vita3K utility. This verifies the implemented model, not Xbox hardware
filter accuracy or audible game content.

Native116 first reached the new boundary; Native117 adds the completed alias
guards and clearer source identifiers and reproduces it. Spatial interface is
`0129601C`, parent references become 137, and all three source masks are active.
Native117's pre-drain snapshot reports:

| Counter | Observation |
|---|---|
| Physical computed / submitted / consumed frames | 5,120 / 5,120 / 4,096 |
| Nonspatial bin 13 submitted / consumed | 5,120 / 4,096 |
| Nonspatial bin 23 submitted / consumed | 3,072 / 2,048 |
| Spatial bin 23 submitted / consumed | 1,024 / 0 |
| Compute / maximum grain time | 316,991 / 70,506 microseconds |
| Deadline misses / observed empty queues | 5 / 4 |
| Nonzero grains / peak / error | 0 / 0 / 0 |

Close returns zero and all terminal graphics snapshots complete. Vita3K then
hits its recurring `2008C8` teardown access after closing the app in both runs;
that is separate from the original strict sound boundary. Computation remains
slower than real time, with explicit gap risk. No physical audio output is
claimed for this lab.

The next strict stop is `37D4BE`, return `21E830`, ESP `005E5EF8`:
descriptor `005E5F34` now requests nonspatial bin 24, flags `100000`, output
`00734218`. The original loop also has a spatial companion and a bin-25 pair.
Extending that loop requires tested simultaneous accumulation and independent
filter/source ownership; those pairs are not accepted by this checkpoint.

## Private evidence and replay

| Native117 artifact | SHA-256 |
|---|---|
| ELF | `18b40d14c4bfd1fac7eb8e70f8923f5d4a525e9a38c9de171319cc7e00c45476` |
| EBOOT | `0de127e75461bf3a78beeca994fbf2db3676263d9ea3f08e697ff05306c5c6a1` |
| VPK | `8860640436867990bb47df846ff7afc65cfad49daf836aa1d67bd5946b78d45e` |
| Guest trace | `3b62a5a93676e3ac54e849ab22585d4dbdfaf0dd78d9fe838a4db11fa724640d` |
| Black presented frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

| Native ARM model comparison | SHA-256 |
|---|---|
| ELF | `f34a2a900984f7f310c7adfdddb4b6b12d2ee5c754d186bf68d8fda59e9be893` |
| EBOOT | `992c2eaa11eb161d3b8537a9cb5e11a9dac8b3735915e80202bdf023dee81ab2` |
| VPK | `44438c2c98c797110a5428dea7f765ae3e86c5bff4086fb31dd3dfce3dd325d3` |
| Result | `9ff5450a178708ec7d7eed1837102a3e8f10bab81620ba209443f48b51cee534` |

Evidence is under `../private/native-117-artifacts`, `native-117-view`,
`native-milestone-117.json`, `audio-spatial-model`, `audio-host` and
`dsp-bringup`. Original audit is `audio-host/probe_original_spatial_fxin2.py`
and JSON. Reference comparison, native utility, exact hash manifest and
validation logs are in `dsp-bringup`, including
`spatial-native-reference-milestone.json`.

From that private directory:

```sh
python3 run_lab.py 117-replay native-117-artifacts/halo2-boot.vpk
python3 run_lab.py stop
python3 dsp-bringup/run_hrtf_model_check.py
```

Use a fresh private build directory and the existing DSP game options plus
`AUDIO_SPATIAL_MODEL=1`. **Diagnostic game packages embed owned executable
content, and the comparison utility embeds owned filter data. Neither may be
uploaded or distributed as a release.**
