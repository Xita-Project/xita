# Halo 2: active spatial FX23 mute

Native120 executes the original spatial FX23 `SetVolume(-6400)` call from
`2AECA6`, then reaches bin24 route setup. **The game still presents only its
first black frame with zero nonzero audio grains; there is no main menu or
gameplay result.** The visible intro remains the separate native106 checkpoint.

## Original behavior and supported change

A private original-code audit executes 2,593 unique instruction addresses
through spatial construction, setup, deferred setters, Play and this volume
call, using the same owned image recorded in the [spatial audit](halo2-spatial-fxin2.md).
Only allocation and command FIFO capacity are isolated. The setter stores
`FFFFE700` in the parameter object's volume field and emits `FFFFFFFF` to all
three packed hardware volume registers (`FE820360/364/368`). All eight decoded
attenuation slots are therefore `FFF`, the pinned model's mute value. The
route list, spatial parameter block and active voice state remain intact.

The adapter accepts only that exact caller, value and active spatial23 object,
after the verified nonspatial23 route replacement. It changes output gains
under the audio worker lock. The voice continues reading its actual FX source,
updating its filter history and counting GP/sink time. Already prepared audio
is retained, and no completed DSP time is discarded. This follows the pinned
[xemu processing order](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/vp/vp.c),
which processes the HRTF filter before applying route attenuation.
It does not implement arbitrary spatial controls, other volume values, Stop
or Release on playing FX voices. Default options and Halo CE are unchanged.

## Validation and next original boundary

All 40 host executables, extra modes and 27 Python checks pass, as do the
private owned-coefficient ABI run, ABI/worker ASan/UBSan and the documented
worker TSan concurrency subset. Tests verify that muting removes the source's
GP contribution while preserving history at the call and advancing it during
later frames, with unchanged active ownership and continuing sink progress.

The full pinned-model comparison changes routing at frame96 and mutes the
spatial source at frame128, while both implementations continue filtering it.
All 2,230,272 GP input comparisons pass on host and compiled Cortex-A9 execution
in Vita3K, including 156,382 clipping cases. This verifies the scoped model,
not exact Xbox hardware filter timing or physical sound output.

Native120 stops at `37C5E4`, return `2AEDD9`, ESP `005E5E50`, on nonspatial24
interface `012A601C`. List `005E5EB0` points to entries at `005E5E70`:
`{6:-6400, 8:-6400, 7:0, 9:-6400}`. The next task is the remaining original
pair configuration in `2AEB20`: bin24 route/volume, spatial24 mute, spatial25
route/volume and nonspatial25 mute. Read-only original-code audits for those
cases are already preserved privately; this checkpoint does not accept them.

Pre-drain counters show 12,288 computed/submitted frames and 11,264 consumed,
all seven source bits active, 748,261 microseconds total computation and a
70,671-microsecond maximum grain. There are 12 deadline misses and 11 observed
empty queues. Nonzero grains, peak and error remain zero. Close returns zero
and all graphics snapshots complete. Computation remains slower than real time.

## Private replay and artifacts

| Native120 artifact | SHA-256 |
|---|---|
| ELF | `b5443dba1de97fa79998e4ead26d86e51c1f154e11df39d824c9bf2741f59853` |
| EBOOT | `9816f1b2cf0ce93ba95df7a0a075d09a0ad0c75a8aef19900c5b0b23bc9b7184` |
| VPK | `abcc9ba7bdbb6b05d55df220e8607dc3a5cba719887da60139a7c9d2be11582f` |
| Guest trace | `276f5f0e8080332d22fb79001a51f933644415b3e16213a59a6df4132932f717` |
| Black frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

| ARM route/mute comparison | SHA-256 |
|---|---|
| ELF | `bbdb50c93693d8519eb26028470729371756deea89fc3bbf9a628b00dc08ea2a` |
| EBOOT | `85bc45db6bb108b23a04e25d0689de525f0517430d19b46a0bdfa2be9af54ec3` |
| VPK | `8417bafb34cfc1f8457af0ced0ab03ffd3b669e746699fe03c8e8c4c32e004a2` |
| Result | `98e43835e5262dd3281d73105a5a090b4c447b47edfb69ec546888f1fb46c00e` |

Evidence is in `../private/native-120-artifacts`, `native-120-view`,
`native-milestone-120.json`, `audio-fx-spatial23-mute`,
`audio-host/fxin2-spatial23-mute-original.json` and the `dsp-bringup/fx-mute*`
and `fx-spatial23-mute*` files. From the private directory:

```sh
python3 run_lab.py 120-replay native-120-artifacts/halo2-boot.vpk
python3 run_lab.py stop
python3 dsp-bringup/run_fx_mute_check.py
```

Use the preceding DSP/spatial options and frozen generated source with private
build directory `audio-fx-spatial23-mute/build`. **The diagnostic game package
and reference utility contain owned executable/filter content and must not be
uploaded or distributed.**
