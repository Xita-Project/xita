# Halo 2: the original FX15..22 loop

Native127 completes all eight additional FXIN2 creations, their single-bin
routes and original Play calls. All fifteen sources have real DSP/sink
progress before the next strict stop. **The displayed first frame is black,
nonzero game audio remains zero, and no main menu or gameplay is demonstrated.**
This extends the [fixed-filter checkpoint](halo2-fxin2-filter.md).

Original `21E4B0` defines source bins15..22 and routes6,7,8,9 repeated twice.
Each iteration returns from creation at `21E9E0`, zero volume at `21E9EE`,
SetMixBins at `21EA1B` and Play at `21EA29`. The descriptor is the existing
24-byte FXIN2 format: flags100000, zero external bytes/format/mix list and the
source bin. Original per-bin oracles each execute 1,417 distinct instruction
addresses through construction, zero volume, one-route replacement and Play.
Allocation and FIFO capacity are isolated; this audits original command
production, not hardware output.

Each source is mono signed24 at48kHz and reads its prior GP frame from
`B000 + (bin-11)*128`. Original MISC is zero, so these eight voices have no
low-pass or spatial filter. The one-entry route has volume0; unused route
slots are muted. Play inserts at the MP-list head. The consumer extends the
existing reverse-creation-order float accumulation and quantizes only the
complete GP input sum. Existing spatial and low-pass histories remain owned
by the original voices.

Creation requires the preceding source to be active and the original two
filters configured. The adapter checks caller, descriptor, mapped input and
output, single expected route, zero volume and exact Play arguments. Each
new source owns a device reference and separate submitted/consumed counters;
Play waits for a real grain tagged with that source. No active Release,
arbitrary route, source, PCM/effects coexistence or spatial commit is added.
Source capacity and stable indices now share `audio_fx_limits.h`, used by
both mixer state and public diagnostic snapshots.

All 40 host executables and 28 focused Python checks pass. Owned-coefficient
ABI and ASan/UBSan cover all fifteen objects and rejection/teardown. Real DSP
worker tests and ASan/UBSan plus the documented TSan subset cover every new
source's sink progress, retained grains, drain and fault behavior. Synthetic
GP tests exercise positive/negative saturation without changing FL/FR.
Private host and compiled Cortex-A9 reference comparisons each match
2,230,272 GP input values across33 cases/192 frames, including staged source
activation, existing filter histories,141,450 clipping cases and sink output.
This validates the supported reference model, not MCPX hardware equivalence.

Native126 is preserved as an **invalid private build-cache experiment**.
Copied dependency files retained old absolute target paths. Consequently,
`audio_vita.o` kept the seven-source layout while the new mixer expected
fifteen. It rejected spatial25 Play and produced inconsistent diagnostic
counters. Those counters are not game behavior or progress evidence.

Native127 uses a separate verified directory. All169 dependency targets were
validated and retargeted, all41 nongenerated runtime objects were removed
and freshly recompiled, and only unchanged generated units/function-table
objects were reused. Every postbuild dependency target was checked against
its actual new object path. The complete target map, removed-object list,
object hashes and build identity are private in
`dsp-bringup/fx-extras-verified-cache.json` and
`fx-extras-verified-build-identity.json`. Future private cache reuse must
retarget and validate **every** dependency target, or rebuild the runtime;
copying `.o`/`.d` files alone is insufficient.

Native127 stops at global buffer creation `37D7DE`, return `22153E`,
ESP `005E5EA4`. Original `221490` sets up a1000Hz mono8-bit buffer routed to
bin14, followed by external data and controls; its public creation wrapper,
format, lifetime and real processing require the next audit. It has not been
accepted or replaced with a silent success object.

At the terminal snapshot, bound/playing masks are7FFF and parent references
149. Computed/submitted/consumed frames are24,576/24,576/23,552. Bin15 has
9,216/8,192 submitted/consumed, decreasing by one1024-frame grain per source
to bin22's2,048/1,024. Total computation is1,457,509us, maximum grain67,578us,
24 deadline misses and23 observed empty queues. Error, nonzero grains and
peak are zero. Close and all snapshots complete; computation is still slower
than real time.

| Native127 artifact | SHA-256 |
|---|---|
| ELF | `e999e8290473cb54849759244db7f7796e09fe189d083aa67a33ff82794396fb` |
| EBOOT | `46de14f74cae678135a9b9f7c4477046f62195e768e416fc5860935c54f76c3d` |
| VPK | `de5c10877c061bebe55579981f3fd76742377d4135c035cb4a5d20946fcff843` |
| Guest trace | `92747e94cf7a41ebe06e8d929426c1b95dbbea555b39072882c56a2a9c3a2dd7` |
| Black scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence includes native126/127 artifacts, views and manifests,
`audio-fx-extras-verified/build`, `audio-host/fxin2-extra*-original.json`,
`dsp-bringup/fx-extras*`, and `native-fx-extras-check`. Build uses the same
options as native125, unchanged `audio-fx-filter/generated` and image,
and the verified runtime directory. From the private directory:

```sh
python3 run_lab.py 127-replay native-127-artifacts/halo2-boot.vpk
python3 run_lab.py stop
```

**Diagnostic packages embed owned game content and must not be uploaded or
distributed.** Native126's failed build and native127's validated build remain
separate, immutable evidence.
