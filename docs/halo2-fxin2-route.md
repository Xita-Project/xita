# Halo 2: active FX23 route replacement

Native119 passes the original `SetMixBins` call from `2AEC87` and following
zero-volume call from `2AEC95`. The nonspatial FX23 voice now feeds GP bin 6
instead of FL/FR. The next strict stop is a volume request on its spatial
companion. **The frame remains black and game audio samples remain zero;
there is no main menu or gameplay result.**

## Original behavior and implementation

The earlier checkpoint's description as a per-bin volume update was imprecise.
Public `37C5E4` reaches `37BF7B`: `37BC89` replaces the route count, ordered bin
list and indexed gains, then `381C23` refreshes the active hardware voice.
The owned image and exact creation/Play contracts remain those recorded in
the [seven-source checkpoint](halo2-fxin2-loop.md).

A private original-code audit executes 1,418 unique instruction addresses
through nonspatial construction, Play, the route setter and zero-volume setter.
The requested list is `{6:0, 8:-6400, 7:-6400, 9:-6400}`. Original `380B97`
converts attenuation to units of 1/64 dB with integer truncation and saturation
to `FFF`. Captured `FE820300=04091D06`, `FE820304=E2000062`, and volume words
`FFFF000F,FFFFFFFF,FFFFFFFF` decode to one unity route and seven mute slots.
The zero-volume call leaves parameters and packed gains unchanged. The audit
isolates allocation and FIFO capacity; it captures original command generation,
not execution of an APU.

The pinned [xemu attenuation implementation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/vp/vp.c)
treats `FFF` as mute. Only this exact list, caller, active bin-23 object and
zero-headroom/volume state are accepted. The mixer changes the voice's output
mask under its existing worker lock. All guest inputs are mapped and copied
before the change. The adapter retains the ordered list and exact requested
gains independently of its effective unity/mute mask.

Already computed or submitted grains retain their previous contents. Future
computed grains use the new destination; no DSP frames, filter history, source
ownership or sink progress are discarded. Other route lists/gains and spatial
changes remain strict boundaries. Halo CE paths and default options are unchanged.

## Tests and native result

All 40 host executables, extra channel modes, 27 Python tests, private owned
coefficient ABI tests, ABI/worker ASan/UBSan and the existing TSan concurrency
subset pass. Synthetic checks verify removal from FL/FR, real addition to GP
bin 6, preservation of the spatial companion and rejection without mutation.
A concurrent test holds a prepared old-route grain across the setter and
checks the next computed grain's changed PCM output.

A separate full pinned-model comparison changes the route halfway through
each of 33 cases and checks 2,230,272 GP input values, including 157,637 clipping
cases. Both host and compiled Cortex-A9 execution in Vita3K pass. The reference
uses the previously documented deterministic single-worker accumulation order.

Native119 reaches `37B66F`, return `2AECA6`, ESP `005E5E50`, interface `0129601C`
(spatial FX23), requesting `FFFFE700` (-6400 hundredths of a dB). That request
remains unsupported here. Pre-drain counters are 11,264 computed/submitted
frames and 10,240 consumed, with all seven source masks active. Computation
takes 698,699 microseconds total, maximum 71,195 per grain, with 11 deadline
misses and 10 observed empty queues. Nonzero grains, peak and error are zero.
Close returns zero and all graphics snapshots complete. These lab observations
do not establish physical sound output or real-time performance.

## Private evidence and replay

| Native119 artifact | SHA-256 |
|---|---|
| ELF | `27e31517c153ea627035f3fc1ac991d56a7843d4c83cdbb82bd97fe101099454` |
| EBOOT | `2d901d03b0b19c9fd2c33574eb9b3781fafcd39f5bb561b584ea42c370823ff9` |
| VPK | `228b7a2573f69c9e35e9b6d84436b10a6a990d175795837c238a1664f5f15c9b` |
| Guest trace | `325687b5ebc8293ba2ae477117bacacbf519424557486da85fa4b13873afe705` |
| Black frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

| ARM transition comparison | SHA-256 |
|---|---|
| ELF | `a835d5ada19d82ffe4b8372dc166405b2c14a0b1b8bd26c3be3f901b940297f5` |
| EBOOT | `61dbef0625db036b8291d0440f64a0bc1d6bf92846578d0b404a76402756ff74` |
| VPK | `5c347b5fba6a6a33495be46193d744ed441c81eb32e9f0c5b025de1d7bad9cfa` |
| Result | `0a027bb86002f04f707853b7eee1be896672a1450137347580894691019f43aa` |

Private files are `native-119-artifacts`, `native-119-view`,
`native-milestone-119.json`, `audio-fx-route23`,
`audio-host/probe_original_fxin2_route.py`, `audio-host/fxin2-route23-original.json`
and `dsp-bringup/fx-route*`. Reproduce from the private directory:

```sh
python3 run_lab.py 119-replay native-119-artifacts/halo2-boot.vpk
python3 run_lab.py stop
python3 dsp-bringup/run_fx_route_check.py
```

Use the previous DSP/spatial build options and frozen generated source with
`audio-fx-route23/build` as the private build directory. **Game packages and
the reference utility embed owned executable/filter content and must not be
uploaded or distributed.**
