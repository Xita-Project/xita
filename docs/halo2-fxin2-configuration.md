# Halo 2: complete original FX pair configuration

The following [native123/124 checkpoint](halo2-fxin2-deferred.md) implements
the original deferred parameter storage and captures the next filter request.

Native122 completes the remaining route/volume calls in original `2AEB20`.
It then stops at a new deferred spatial-parameter setter. **The game still
presents its first black frame, with zero nonzero game audio grains. No main
menu or gameplay is demonstrated.**

## Verified sequence

The seven sources and model retain the contracts in the [prior mute checkpoint](halo2-fxin2-mute.md).
Only these additional original calls are accepted:

| Return address | Object | Operation and effective output |
|---|---|---|
| `2AEDD9` | Nonspatial FX24 | Replace list with `6,8,7,9`; bin7 unity, others -6400/mute |
| `2AEDE7` | Nonspatial FX24 | Retain master volume zero |
| `2AEDF8` | Spatial FX24 | Master volume -6400, all outputs muted |
| `2AEF24` | Spatial FX25 | Replace list with `6,8,7,9,10`; bin10 unity, others -6400/mute |
| `2AEF32` | Spatial FX25 | Retain master volume zero and the bin10 contribution |
| `2AEF43` | Nonspatial FX25 | Master volume -6400, all outputs muted |

Native121 rejected an initial incorrect assumption that spatial25's last
route was also muted. Its captured list proved bin10 is unity. That candidate
stopped before mutation; native122 uses the captured list. The private original
setter audit with those exact inputs emits `FFFFFFFF,FFFFFFFF,FFFF000F`,
confirming the surviving bin10 slot. The older all-muted probe remains an
unobserved test input and is not evidence of the game's selected configuration.

Original-code audits of FX24 route/volume, spatial24 mute, spatial25 route/zero
volume and nonspatial25 mute execute 1,418, 2,593, 2,689 and 1,322 unique original
instruction addresses respectively. Allocation and FIFO capacity are isolated;
no APU execution is claimed by these audits. All use the owned image identity
recorded in the previous checkpoints.

The adapter validates caller, object identity/state, mapped list and every
route/gain before changing the real mixer. Ordered bins and requested gains
are retained. Mixer changes are serialized with computation and require the
preceding pair state. Prepared grains are retained. Muted sources continue
reading FX samples, updating spatial histories where applicable and counting
GP/sink progress. The final outputs are bin13 to0..5, nonspatial23 to6,
nonspatial24 to7 and filtered spatial25 to10. Other gains/routes, arbitrary
spatial changes and active Stop/Release remain strict boundaries.

## Validation and native result

All 40 host executables, extra modes and 27 Python checks pass. The private
owned-coefficient ABI test covers each caller/list field and preserved object
lifetime. ABI/worker ASan/UBSan and the existing TSan concurrency subset pass.
Synthetic GP tests check final destinations, muted-source timing and all three
filter histories. A concurrent worker test checks real changed PCM output
after retained old grains drain.

A separate pinned-model reference executes the whole timed route/mute sequence
and passes all 2,230,272 GP input comparisons on host and Cortex-A9 execution
in Vita3K, including 123,422 clipping cases. It retains the documented
single-worker accumulation and fixed-filter limitations.

Native122 reaches `37C6E5`, return `2AEF6A`, ESP `005E5ED0`, on spatial25
interface `012D601C`, with arguments `470060,1`. Original `37C0E9` copies nine
words to the spatial parameter block at offsets80..A0 and marks deferred bits;
this new setter is not implemented by this checkpoint.

Pre-drain counters report 15,360 computed/submitted frames and 14,336 consumed,
all seven source masks active, 950,308 microseconds total compute and 73,713
maximum per grain. There are 15 deadline misses and 14 observed empty queues.
Nonzero grains, peak and error are zero. Close returns zero and all graphics
snapshots complete. Computation remains slower than real time; no physical
audio output or menu rendering is claimed.

## Private artifacts and reproduction

| Native122 artifact | SHA-256 |
|---|---|
| ELF | `fadfaf428ac93987a68533329d4fca0689b63184fb932e7479ce3a2a247f6591` |
| EBOOT | `6b200dabde02c982b98dd5566d44999b032e8846a2ce1dd2e94ee48699de3de9` |
| VPK | `5cd5ca37075ebf81a303a68a893880c89a850b93dd8ac50301c36da5ffc780e5` |
| Guest trace | `f07d62bc9ceafedd7458fe5da659a84cdce31e855be141f49d91d06ab2afbeb0` |
| Black frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

| ARM complete-sequence comparison | SHA-256 |
|---|---|
| ELF | `949244f87d53909a5ca716771f709f028b007d9779457c2d46242788f5e3952b` |
| EBOOT | `679ac429a2751c3a5eda8af8cd510cd74a50a1eeb8a73dfffea6697ee7c8da71` |
| VPK | `77a02d80f9607e818b1a137eb5f83c1428097e30d9f2ea509319c162d6b7b0f2` |
| Result | `65a25671e17ef1acd44eed03cade4e9f5421336767ca0879bec3750a5bc88366` |

Private evidence is `native-121-artifacts`, `native-122-artifacts`,
`native-122-view`, `native-milestone-122.json`, `audio-fx-remaining-verified`,
the `audio-host/fxin2-*24*`, `fxin2-spatial25-bin10-original` and
`fxin2-nonspatial25-mute-original` audits, and `dsp-bringup/fx-remaining*`.
The final host/sanitizer logs have suffix2. From the private directory:

```sh
python3 run_lab.py 122-replay native-122-artifacts/halo2-boot.vpk
python3 run_lab.py stop
python3 dsp-bringup/run_fx_remaining_check.py
```

Build with the prior DSP/spatial options and frozen generated source, using
`audio-fx-remaining-verified/build`. **Diagnostic game packages and reference
utilities embed owned executable/filter content and must not be distributed.**
