# Halo 2: second FXIN2 source and simultaneous mixing

Native115 executes the original bin-23 FXIN2 creation and Play after the first
bin-13 source, then stops at the next spatial-buffer descriptor. The game has
submitted only its first black frame and zero nonzero audio grains at this
checkpoint. **No main menu or gameplay is demonstrated.** The original visible
Microsoft intro remains [native106](halo2-visible-intro.md).

## Original contract and implementation

The owned XBE SHA-256 remains
`03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d`.
Original caller `21E4B0` creates the second FXIN2 buffer at return `21E830`
and plays it at `21E842`. Its descriptor is 24 bytes, flags `100000`, input
bin 23, with zero external bytes, null format/list/aggregation. It keeps the
constructor's unity FL/FR routes, volume zero and headroom zero. A private
Unicorn oracle executes the original constructor chain from `37CEF1`, with
170 unique instruction addresses and no function replacements. Cases for bins
13, 23, 24 and 25 confirm those defaults and mono 48 kHz signed-24 samples in
four-byte containers. Only the first two bins are accepted by this adapter.

The first source retains its independently verified six unity routes. Both
sources read the same previously completed real GP frame, contribute to their
respective input bins, and advance the GP exactly once per 32 samples. The two
signed-24 unity contributions fit exactly in int32 and in the pinned reference's
binary32 accumulator. Clipping occurs after all contributions, to the signed-24
range. A private comparison against the actual pinned
[xemu conversion header](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/fpconv.h)
passes 131,072 sample cases, including 16,544 positive and 16,444 negative clips.
The reference [voice accumulation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/vp/vp.c)
and [GP input conversion](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/dsp/gp_ep.c)
establish that ordering. GP FL/FR monitor output remains the actual stereo sink
source; EP/AC3 and full surround output remain unsupported.

Each source has its own binding, active flag, frame count and guest lifetime.
A prepared grain retains its active-source mask. Starting the second voice
cannot relabel an old grain or satisfy Play with the first voice's submission.
Play waits for a newly submitted grain containing that source. Sink consumption
is attributed using the retained mask, without double-counting physical frames.
Inactive Release preserves the other owner. Active Release/Stop, spatial
processing, bins 24/25, arbitrary routing/gain and concurrent PCM playback with
loaded effects remain strict boundaries.

The ownership mutex covers source state, DSP execution, snapshots and queries.
The worker's scheduling decision is captured under that mutex, and terminal
errors stop further computation before draining. No Halo CE path changes.

## Validation

All 39 host executables (41 invocations including extra modes), 26 focused
Python tests, FX ABI ASan/UBSan and worker ASan/UBSan checks pass. Synthetic
fixtures run the actual DSP core and concurrent worker. They cover signed
clipping, opposite-sign sums, route separation, one GP advance for two sources,
resource ownership, guest ABI/aliasing, old prepared-grain rejection, real sink
progress, faults and final drain. The concurrent fixture observes stereo output
3,906 with both sources versus 1,953 with only the first.

The TSan concurrency subset also passes. GCC TSan fails its own longjmp-buffer
check inside the injected DSP-fault fixture; that fixture alone is excluded
from the TSan run and remains covered by normal and ASan/UBSan runs. Production
fault handling is unchanged. No owned code bytes are tracked by these tests.

The final native utility uses the same worker and original GP, with synthetic
one-frame input pulses. Its result is:

| Final two-source utility | Observed result |
|---|---|
| Physical computed / submitted / consumed frames | 4,096 / 4,096 / 4,096 |
| Source 13 submitted / consumed | 4,096 / 4,096 |
| Source 23 submitted / consumed | 3,072 / 3,072 |
| Nonzero grains / peak | 4 / 7,039 |
| Compute / maximum grain time | 250,574 / 69,758 microseconds |
| Deadline misses / empty queues after computation | 4 / 3 |
| Error / close | 0 / 0 |
| DSP instructions / canonical state | 3,112,502 / `4492FD6FE6F72687` |

A host replay matches that exact DSP state, peak, 8,192 nonzero PCM samples and
PCM FNV-1a `593972FC883A2A25`. It also distinguishes the sources: the bin-23
pulse exists for 32 samples while its voice is inactive, and the original GP
clears it before the second Play. There are zero nonzero source-23 samples while
that voice is active. Thus this native probe proves second-source ownership and
consumption, while its nonzero audio comes from source 13; synthetic core tests
separately prove simultaneous nonzero mixing. Vita3K uses the isolated lab's SDL
dummy driver, so this is emulated-sink evidence, not physical speaker output.
About 85.3 ms of output requires about 251 ms of computation; real-time audio
is not achieved and gaps remain possible.

## Native game boundary

Native114 first reached this boundary; final Native115 includes the completed
worker lock/error review and reproduces it. Bin-13 interface is `0127601C`;
bin-23 interface is `0128601C`, reading scratch `B600`, with parent references
136. Both Plays return after actual source-tagged submission.

Native115 stops strictly at `37D4BE`, return `21E88A`, ESP `005E5EF8`.
Descriptor `005E5F4C` requests size 24, flags `00100010`, bin 23, zero bytes,
null format/list/aggregation, output `00734220`. This is the spatial companion;
it has not been accepted or treated as silent. The inspected continuation sets
maximum and minimum distance to FLT_MAX with deferred flags, then calls Play.
Original spatial routing, attenuation and hardware setup are the next audit.

At the stop, 3,072 physical frames are computed/submitted, with 2,048 observed
consumed. Source 13 is 3,072/2,048 submitted/consumed; source 23 is 1,024/0 at
this pre-drain snapshot. Compute is 198,557 microseconds, maximum 72,553, with
three deadline misses and two observed empty queues. Close returns zero and
all snapshots complete. Native114 had a later Vita3K teardown SIGSEGV after
its strict stop and completed close; Native115 closes the app normally.

## Private artifacts and exact replay

| Native115 artifact | SHA-256 |
|---|---|
| ELF | `80008d363bed993b2c04f5dee3e0f5951e053a98a1bbe4da1cdb00e7048e2957` |
| EBOOT | `b310d6dca7040fcc75cdc433fa0c94c180a65499183903c6f6afe0ac7ffd5253` |
| VPK | `d0a03238b305275bd863ace695f23676819ad8dac5cd9ea1362f835adecedcf7` |
| Guest trace | `98fc47e1eb9d925f67bf1f1a4c125fe083d6ce716ed77cbb7b6b0cca6d0851e1` |
| Channel JSON | `a2b3a19912c99b5a3e79150d46afd479d4b945b4679f2a369fa6ec0f4655bf00` |
| Black frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

| Final native two-source utility | SHA-256 |
|---|---|
| ELF | `06c974c646e383c6913406157e6c73d03cc03be1e84250743095edc2f203c5d0` |
| EBOOT | `38e4241fee70b2c951d8ff939f3549d3e486cc7374b487e92f7449816fffe443` |
| VPK | `3c690fd4f35524940ec07d1cdcdab1661bdf0fbb723263cc1ba6b2a84f9e54ed` |
| Result | `979d877174857077b1e5031e72f7dfafed72b43cf8335799107e25f7b09d03e4` |

Evidence is under `../private/native-115-artifacts`, `native-115-view`,
`native-milestone-115.json`, `audio-fx23`, `audio-host` and `dsp-bringup`.
The latter contains `fx23-native-milestone.json`, final probe, host/sanitizer/
TSan logs, `fx23-reference.log` and `check-fx23-signal.log`. The constructor
oracle is `audio-host/check_original_fxin2_defaults.py` and its JSON.

From that private directory:

```sh
python3 run_lab.py 115-replay native-115-artifacts/halo2-boot.vpk
python3 run_lab.py stop
python3 dsp-bringup/run_fx23_probe_final.py
```

The utility build uses target `fx-probe`, `FX_PROBE_SECOND=1`, private `BUILD`
and owned `DSP_ASSET`; the default first-source probe remains available.
**These diagnostic packages embed owned executable game/DSP content and must
not be uploaded or distributed as releases.**
