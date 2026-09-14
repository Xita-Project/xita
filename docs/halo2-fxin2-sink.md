# Halo 2: first FXIN2 source connected to the real sink

Native113 executes the original first FXIN2 creation, unity volume, six-bin
routing and Play sequence, then continues through the original effect-state
queries. The GP interpreter produces real frames for the Vita stereo sink.
The game supplies silence at this checkpoint and still displays only its first
black frame. **No main menu or gameplay is demonstrated.** The visible original
Microsoft intro remains [native106](halo2-visible-intro.md).

## Verified boundary

Owned XBE SHA-256 is
`03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d`.
The accepted descriptor has size 24, flags `100000` (FXIN2), input bin 13,
zero bytes, null format, null mix-bin list and no aggregation. The exact
creation/volume/routing/Play return addresses are `220C26`, `220C37`, `220CAA`
and `220CB5`. Other callers, formats, input bins, gains and flags stop.

The [previous signal audit](halo2-dsp-signal-frames.md) establishes the original
monitor's FX output mapping: bin 13 reads the prior completed frame at scratch
`B100`. Original `37CDCA`/`37CEF1` settings establish mono 48 kHz,
signed-24 samples in four-byte containers, a 32-sample source, and zero
headroom. `37C825`/`37BC89` select default unity FL/FR routes. The game then
selects six independent unity routes to bins 0 through 5. These are six copies
of the mono source into separate GP input bins; they are not summed into a
stereo substitute.

The original public Play flags are zero. Original `382D22` explicitly forces
loop flags 1 for FXIN2, then dispatches the hardware activation. A private
Unicorn oracle runs the owned public/common/voice dispatch for ten public-flag
and setup-HRESULT cases, checking preserved callee-saved registers, stack,
forced-loop arguments and failure propagation. Only locking, already-completed
resource setup and the final hardware activation are isolated in that oracle;
it is not evidence of APU execution. The original effective-volume setter also
confirms the supported unity case with zero headroom.

`audio_fx.c` executes every real GP frame in order. Its output conversion takes
the GP front-left/right monitor taps and preserves signed floor quantization
from 24 to 16 bits. This representation follows the pinned
[xemu GP monitor output path](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/dsp/gp_ep.c).
It does not implement EP/AC3 encoding or full surround output. The separate
PCM mixer's user master-volume adjustment is not inserted into this GP path.

## Worker and lifetime

The adapter owns one checked FX source and its guest common header; the device
retains ownership of the real DSP engine. Creation binds an inactive source.
It requires all PCM/stream voices inactive and headroom zero for the six
routes. The accepted Play waits for an entire processed grain to be accepted
by `sceAudioOutOutput`. No guest sample pointer or dummy WAVE format enters the
PCM mixer for FXIN2.

The worker computes the next grain into separate storage while the preceding
grain plays, retaining it until the prior sink grain drains. It never drops
computed DSP time to catch up with wall time. A partial grain from a DSP fault
is never submitted. Consumption counters follow only successful
`GetRestSample` observations. Processing that exceeds a grain deadline and
observed empty queues after processing have separate counters; these are not
hardware underrun interrupts or measured durations of silence. This version
has audible-gap risk because the interpreter is slower than real time.

One mutex serializes DSP execution, routing/ownership changes and complete
GetEffectData reads. Final close joins the worker and verifies the retained
grain drained before freeing its engine or storage. Inactive Release is
supported; active Release, Stop, cursor queries, spatial processing, additional
FX sources, packet submission and PCM playback with loaded effects remain
strict boundaries. Existing Halo CE code and default paths are unchanged.

## Validation and native results

All 39 host executables and additional channel modes pass. New tests use a
synthetic DSP program and the real interpreter/worker to check signed extrema,
six distinct routes, failed ownership changes, concurrent effect queries,
slow processing, sink failures, retained storage and exact consumed-frame
counts. The FX ABI and concurrent-worker ASan/UBSan tests pass. All 26 focused
Python audio-hook/callback-root tests pass. No owned game bytes are tracked.

A separate native utility feeds one synthetic pulse through the owned DSP,
then uses the same FX source/worker/sink path. It reports:

| Native FX signal probe | Result |
|---|---|
| Computed / submitted / consumed sample frames | 4,096 / 4,096 / 4,096 |
| Nonzero stereo grains / peak | 4 / 7,039 |
| Compute time / maximum grain time | 253,967 / 73,744 microseconds |
| Processing deadline misses / empty queues after compute | 4 / 3 |
| Sink error / close result | 0 / 0 |
| DSP instructions / final canonical state | 3,112,502 / `4492FD6FE6F72687` |

A host replay produces the identical canonical DSP register/stack/RAM/scratch
state, 8,192 nonzero PCM samples, peak 7,039 and little-endian PCM FNV-1a
`593972FC883A2A25`. Vita3K uses its real audio queue with the isolated lab's SDL
dummy output driver. This proves nonzero samples and consumption through the
emulated Vita sink, not physical speaker output. The probe represents about
85.3 ms of audio with about 254 ms of computation; real-time output is not met.

Native113 then confirms the original game calls:

- First FX interface `0127601C`; parent references rise to 135.
- Original GetEffectData subsequently reads effects 11 through 14.
- At the next strict stop: 2,048 computed/submitted frames, 1,024 observed
  consumed, 132,042 microseconds compute, maximum 70,003 per grain, two missed
  deadlines, one observed empty queue, and zero nonzero grains.
- Terminal worker close returns zero and all graphics snapshots complete.
  The emulator closes the app without the earlier teardown crash in this run.

The exact next stop is CreateSoundBuffer `37D4BE`, return `21E830`,
ESP `005E5EF8`. Descriptor `005E5F34` requests another nonspatial FXIN2 source,
bin 23 (`17` hex), flags `100000`, output `00734214`. The first source remains
active. This supersedes the earlier *inference* that a spatial source would be
next. Original caller `21E4B0` then invokes Play with default routes at return
`21E842`; that continuation has been inspected but not executed yet. The next
bounded task is validating this additional source's routing and simultaneous
mixing/ownership, including saturation, before accepting it.

## Private evidence and replay

| Native113 artifact | SHA-256 |
|---|---|
| ELF | `dc4c9ae22627a295d98afcd30dac24442d52f97802e36380b8822b2ac6255690` |
| EBOOT | `0a5ff2eda0a53de39ff5110490e92cfbb8dd94f24789afa9342ad8f0fa9c44c0` |
| VPK | `02b76e5723bb5bdb0067c8fd8a114ab049c6ba833b6f5dcfd6c474341d14fa43` |
| Guest trace | `16b903297383d3faea8d8793df3fd8d8bad2c9035a445d0557536443a98b70d0` |
| Channel JSON | `a2b3a19912c99b5a3e79150d46afd479d4b945b4679f2a369fa6ec0f4655bf00` |
| Black presented frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

| Native FX signal utility | SHA-256 |
|---|---|
| ELF | `ee578ce9e6d7f9e0f7869491504cbcf94a89774a1de886eff79b015c98be7ad4` |
| EBOOT | `3dae4c7636a9c01965b2b2be2325741956f0a28066ba303837feacf21da9101d` |
| VPK | `3f6a1fed678b1502bc39d58e3fa5cfc0ff79e657a79f6d70b3a75fc02efb7d88` |
| Result | `82898ed2aea90d7c48bcd77ffdd3bbbad1e0a5c9a210dcb37ce3719e9f97cfb8` |

Evidence is under `../private/native-113-artifacts`, `native-113-view`,
`native-milestone-113.json`, `audio-fxin2` and `dsp-bringup`. The latter holds
`fxin2-native-milestone.json`, the utility build/results, host and sanitizer
logs and `check-fxin2-signal.log`. Original-call oracle evidence is in
`audio-host/fxin2-play-original-check.json`.

From the private directory, replay the exact game with
`python3 run_lab.py 113-replay native-113-artifacts/halo2-boot.vpk`, then
`python3 run_lab.py stop`. Replay the utility with
`python3 dsp-bringup/run_fxin2_probe.py`. Both use only the owned `:111` lab.
The existing [DSP game build options](halo2-dsp-game-init.md) apply; utility
build target is `fx-probe` with private `BUILD` and `DSP_ASSET` paths.
**Both diagnostic packages embed owned executable game/DSP content and must
not be uploaded or distributed as releases.**
