# Halo 2: original seven-source FX loop

Native118 completes the original FXIN2 creation/Play loop, including both
nonspatial and fixed spatial voices for bins 23, 24 and 25. Together with bin
13, all seven sources feed the actual GP interpreter and emulated Vita sink.
The next strict stop is a route-list replacement with per-bin gains. **The presented frame remains
black, game audio remains zero, and no main menu or gameplay is demonstrated.**
The visible original intro checkpoint remains [native106](halo2-visible-intro.md).

## Original loop and supported model

The owned XBE is the same image recorded in the [spatial audit](halo2-spatial-fxin2.md).
Caller `21E4B0` repeats the already observed bin-23 sequence for bins 24 and 25:
nonspatial creation returns at `21E830`, Play at `21E842`, spatial creation at
`21E88A`, deferred FLT_MAX maximum/minimum distance at `21E89D`/`21E8AC`, and
spatial Play at `21E8BA`. Descriptor, format, flags and zero Play arguments are
otherwise unchanged. Private original-code audits for each bin execute 2,559
unique instruction addresses, including construction, setters and Play.
Only allocation and FIFO capacity are isolated; captured writes are evidence
of command generation, not APU execution.

Each original Play inserts its voice at the MP-list head (`FE820120=0003FFFF`).
The mixer therefore visits sources in reverse creation order, using the pinned
xemu model's **single-worker** float accumulation before the final signed-24
conversion. It does not claim the exact reduction order of a parallel emulator
worker pool or hardware rounding. Separate spatial histories retain unrounded
filter output until all contributions have accumulated. Early per-voice
quantization changes results and is deliberately avoided.

Nonspatial bin 13 feeds six unity routes; nonspatial bins 23..25 feed FL/FR.
Each spatial companion uses the same checked symmetric, zero-delay owned
filter and feeds bins 6, 7 and 10, with 8 and 9 muted. All source reads precede
one shared GP frame. The explicit `AUDIO_SPATIAL_MODEL=1` option and exact
state/caller checks remain required. Dynamic spatial changes, arbitrary
routes/gains, active Stop/Release, other FX inputs and PCM coexistence with
loaded effects remain unsupported.

Creation requires the preceding original source to be active. Each object has
its own device reference, active bit, history and sink progress. Inactive
rollback leaves other owners intact. Existing prepared grains cannot satisfy
a new source's Play; terminal close drains and joins before freeing the DSP.

## Validation

All 40 host executables, additional channel modes and 27 focused Python tests
pass. ABI tests with private owned coefficients cover all seven objects,
deferred setters, exact Play arguments, aliases and ownership. Worker tests
exercise actual GP output, source-tagged retained grains, clipping, rollback
and drain. ABI and worker ASan/UBSan pass; the worker concurrency TSan subset
passes with the previously documented injected-longjmp exclusion.

A separate comparison uses the full pinned xemu HRTF/FP conversion code and
the real synthetic GP fixture: 33 coefficient cases, 192 frames each and
2,230,272 GP input values. It passes both on the host and as Cortex-A9 code in
Vita3K, including 162,922 clipping cases. It observes 70,515 differences from
early spatial quantization. The four unity nonspatial sources produce no
differences from exact integer accumulation in this comparison. This validates
the implemented model, not hardware filter accuracy or nonzero game audio.

## Native118 result

All seven source bits are bound and playing (`7F`), parent reference count is
141, and every source has actual consumed frames before the terminal snapshot.

| Pre-drain counter | Observation |
|---|---|
| Computed / submitted / consumed frames | 10,240 / 10,240 / 9,216 |
| Bin13 submitted / consumed | 10,240 / 9,216 |
| Bin23 nonspatial, spatial | 8,192 / 7,168; 6,144 / 5,120 |
| Bin24 nonspatial, spatial | 5,120 / 4,096; 4,096 / 3,072 |
| Bin25 nonspatial, spatial | 3,072 / 2,048; 2,048 / 1,024 |
| Compute / maximum grain time | 628,535 / 70,369 microseconds |
| Deadline misses / observed empty queues | 10 / 9 |
| Nonzero grains / peak / error | 0 / 0 / 0 |

Computation remains slower than real time. Close returns zero and terminal
snapshots complete; the lab makes no physical sound-output claim.

The next strict stop is `37C5E4`, return `2AEC87`, ESP `005E5E50`, on nonspatial
bin-23 interface `0128601C`. List `005E5EB0` points to four entries at `005E5E70`:
`{6:0, 8:-6400, 7:-6400, 9:-6400}`. Their relationship to the object's existing
routes must be established from the original setter before accepting them.
No return value is fabricated to advance this boundary. The later
[native119 route audit](halo2-fxin2-route.md) confirms this is `SetMixBins`
replacement semantics, not an update limited to previously assigned bins.

## Private evidence and replay

| Native118 artifact | SHA-256 |
|---|---|
| ELF | `059d8028dda9cca94b3d76746a5343d668d17114e9eac59c467cdb49e1bd48f1` |
| EBOOT | `fcb45b718858d2b1cdc9480c0e001ab8b299d4001894f90cacdc1a4fd5420de5` |
| VPK | `463b5aef2491ecfbb33d8f025107478e3efe9a301da0f7b8a3a0d23ad3f575b4` |
| Guest trace | `a6081a67f9ff030348b4d06a65c49b18d6ae16b56ee5833e4db848f3bf300626` |
| Black presented frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

| ARM seven-source comparison | SHA-256 |
|---|---|
| ELF | `5c9cdbfc6651230555314c48fada49970658b69065d59273eb8c48b2e793c662` |
| EBOOT | `41e095a6d6f0afc0910123c8a4020dc31a87b0a8f5cccc480cb54d156cb735a3` |
| VPK | `75624fa4a235cf6d33a5cf4b47747de7d14c05f5fc69644235d546109806d189` |
| Result | `630219769cf59095096f0ca327fab779062aa46913e2fe9784c25dc6035170d2` |

Evidence is under `../private/native-118-artifacts`, `native-118-view`,
`native-milestone-118.json`, `audio-fxin2-loop`, `audio-host/fxin2-loop-bin*.json`
and `dsp-bringup`. Validation logs and the ARM utility/manifest have the
`fx-loop` prefix. From the private directory:

```sh
python3 run_lab.py 118-replay native-118-artifacts/halo2-boot.vpk
python3 run_lab.py stop
python3 dsp-bringup/run_fx_loop_check.py
```

The game build uses the existing frozen `audio-fxin2/generated` and DSP options,
plus `AUDIO_SPATIAL_MODEL=1`, with a fresh `audio-fxin2-loop/build` directory.
**Diagnostic game packages embed owned executable content and the comparison
utility embeds owned filter data. Neither may be uploaded or distributed.**
