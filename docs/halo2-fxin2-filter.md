# Halo 2: the original fixed FX23/24 low-pass setting

Native125 executes both original filter requests, then stops at the next
FXIN2 creation. **This run presents a black first frame and reports zero
nonzero audio grains. No main menu or gameplay is demonstrated.** The earlier
[visible intro](halo2-visible-intro.md) remains a separate checkpoint.

Original public SetFilter `37B68B` takes two stack arguments and returns with
`ret8`. Caller `2AEFBC` uses nonspatial23, followed by `2AEFCD` on nonspatial24.
The mapped 24-byte descriptor is `{1,0,0,8000,0,0}`. Full original execution
through `37B1E6`, `37A616` and `381710` copies six words and packs voice MISC
`00010000`, FC0 `80000000`, FC1 zero. Observed writes are `FE820318=00010000`,
`FE820374=80000000`, `FE820378=00000000`. The wrapper's 28-byte SHA-256 is
`5c0809339e8b04993352a3357f95cfda7dc11ead13d354d701ad997f272f52cd`.
Private original-code auditing isolates allocation and FIFO capacity only;
it is not hardware/output validation.

The pinned [xemu voice processor](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/vp/vp.c)
selects low-pass processing for this mono mode1 voice. Its signed cutoff and
resonance decode to `f=q=1`. This enables processing rather than bypassing it.
The project implements only that fixed mathematical state-variable recurrence:
normalize by the nearest float to `sqrt(.51)`, damp the band state cubically,
advance high/band/low integrators, then clamp the output. Separate band/low
histories belong to nonspatial23/24 and survive repeated identical setters.
The resulting steady signal is attenuated; hardware equivalence and arbitrary
filter settings are not established.

Model provenance is the recurrence attributed to Steve Harris and
andy@vellocet in the [SWH source](https://github.com/swh/ladspa/blob/master/svf_1214.xml),
as used by the pinned [xemu filter reference](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/vp/svf.h).
The xemu header labels itself GPLv2; it remains in the private comparison
fixture and is not imported into this repository. `audio_filter_model.h`
is a small project implementation of this fixed mathematical case, not a
copy of the general filter header. This is not described as a clean-room
hardware reconstruction. The existing separately attributed HRTF source and
its license remain unchanged.

`AUDIO_FILTER_MODEL=1` is explicit and requires the real DSP backend. Without
it the call remains a strict stop. The adapter validates caller, live object,
configured routes, mapped descriptor and every coefficient before changing
the worker state. The worker serializes selection with DSP processing; already
computed/submitted grains are retained. Filtering occurs before route gain and
GP input quantization. Active release, immediate spatial commit, other filter
modes/coefficients and arbitrary sources/routes remain unsupported.

All 40 host executables and 28 focused Python tests pass. Owned-coefficient
ABI tests check cross-page unaligned input, full context, unchanged input,
rejected coefficients/callers/backend failure, and repeated selection. A
separate build proves the disabled guard still rejects the valid descriptor.
ABI and worker ASan/UBSan pass; the worker's documented TSan subset passes.
Actual GP tests verify distinct filtered bins6/7 and unchanged FL/FR routing.
Private host and compiled Cortex-A9 reference comparisons each match
2,230,272 GP input values across 33 cases and 192 frames, including all route,
mute and filter transitions, exact independent filter histories, repeated
selection, signed saturation and sink samples. Reference correctness does not
establish MCPX hardware equivalence.

Native125 reaches creation `37D4BE`, return `21E9E0`, ESP `005E5EF8`.
Descriptor `005E5F4C` is `{24,100000,0,0,0,15}`, output `0073422C`, outer zero.
The original loop defines eight FX15..22 sources and single routes6/7/8/9
repeated; these require a new audited source/ownership extension. The strict
stop preserves the seven active sources (`bound=playing=7F`). Pre-drain
frames are 14,336 computed/submitted and 13,312 consumed; total computation
1,199,521us, maximum grain93,040us, 14 deadline misses and 13 observed empty
queues. Error, nonzero grains and peak remain zero. Close and snapshots
complete. Computation remains slower than real time.

| Native125 artifact | SHA-256 |
|---|---|
| ELF | `7212d5aa21d98401d12b412876dc433871b92709894d12cbc8bac10dd77e9ed5` |
| EBOOT | `6109e4efc7f04bebace33b6fa0de87c1f6bd8b274dec18b15872887b20f0ee9b` |
| VPK | `2c88c4ff7ec9c3d4fffe14975282b3013c391e6898ff134b832c699bf8dbe5a6` |
| Guest trace | `f3235c95a14d6c2d31d2a5135c58f2ba688bd2b801c73d76a472bf63559b84cd` |
| Black scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence: `native-125-artifacts`, `native-125-view`, its manifest,
`audio-fx-filter`, `audio-host/fxin2-filter23-original.json`,
`dsp-bringup/fx-filter*`, `check_fxin2_filter_reference.c`, and
`native-fx-filter-check`. Regeneration changes only `code_105.c`; 131 other
files were compared byte-for-byte before build-cache reuse.

From the private directory, replay the exact archive with:

```sh
python3 run_lab.py 125-replay native-125-artifacts/halo2-boot.vpk
python3 run_lab.py stop
```

Build uses the existing host-channel, quad, real audio/DSP, spatial and
multibin-unavailable options, plus `AUDIO_FILTER_MODEL=1`,
`audio-fx-filter/generated`, its image and build directory. **Diagnostic
packages embed owned game content and must not be uploaded or distributed.**
