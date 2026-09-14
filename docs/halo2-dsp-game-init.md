# Halo 2: original DSP initialization inside the game

Native107 connects the tested interpreter to the original `191300` sound
initializer. The owned monitor executes its actual image upload, initialization
and command acknowledgment. Four original `GetEffectData` calls read the resulting
effect RAM. The next strict stop is listener position method `37D598`, caller
`2207BF`. This run presents one black frame and exits before the intro. It does
not display the main menu. Native106 remains the separate
[visible Microsoft intro checkpoint](halo2-visible-intro.md).

`AUDIO_DSP=1` requires `AUDIO_HOST=1` and conflicts with the explicit unavailable
effects probe. Default builds and Halo CE are unchanged. The adapter accepts only
the fingerprinted download call, `DSPImage`, locations 9/10 and flags 1. It executes
the private prepared asset before allocating or publishing the descriptor. The
15 original maps point into owned guest views copied from the interpreter's actual
initialized X/Y/P banks and scratch. These are read-only initialization views;
there is no ongoing DSP frame scheduling, dynamic effect upload or voice routing.
Play with a loaded DSP image stops explicitly before mutating a PCM voice.

`GetEffectData` uses the live interpreter state, validates mapped output, physical
aliases and per-effect bounds, and preserves the original invalid-index HRESULT
`88780032`. A private original-code oracle executes the public wrapper, internal
getter, index check and copy for 48 fixtures, including unaligned byte ranges and
invalid indices. The allocator failure path destroys the unpublished interpreter
and leaves the output untouched. Final device Release joins/closes the real audio
worker before freeing the guest views and interpreter. Other unreviewed DirectSound
methods remain terminal.

Native107 reports descriptor `00946000`, 868,352 owned guest bytes, 15 effects,
39,364 instructions, 105 completed transfers, command zero and canonical state
fingerprint `B589B482E35B1F53`. Calls from `1913D5`, `1913EB`, `191400`, `191416`
read eight bytes at offset `20` from effects 4–7. The terminal call has device
`00936008`, position `(0,0,0)`, deferred flag 1, ESP `005E5EC4`. The worker closes
at the stop; its two silent grains do not demonstrate audible effects.

All 34 host executables plus extra channel modes pass. Synthetic adapter tests
cover CPU/FPSCR and stack preservation, actual shared mixer ownership, bank/map
exports, all 15 queries across noncontiguous guest pages, range/alias rejection,
allocation rollback and final lifetime. Their injected DSP provider tests the
adapter contract; separate real interpreter/probe tests establish execution.
Adapter and updated interpreter fixtures pass ASan/UBSan. The H2/profile/LOOP/SIMD
Python regressions also pass.

| Private native107 artifact | SHA-256 |
|---|---|
| ELF | `04e310767a2abc9f4c294c0ca7c42922613168450afbe134f5dc865db736bad5` |
| EBOOT | `7fffa018b2253c349c7ab75eb0ade4abd324c3aff7884102a36fd3ea9e764416` |
| VPK | `082f6118252dc9d34e519991b7d72895c9e08a6b3044d5325e5b59af5f129eb9` |
| Boot trace | `27a5be7a0118ba5d7a90a64bb782b7a32bde94a0d959aa160fd9afbbf8b620b3` |
| Decoded channel | `a2b3a19912c99b5a3e79150d46afd479d4b945b4679f2a369fa6ec0f4655bf00` |
| Last black scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence is `../private/native-107-artifacts`, `native-107-view`,
`native-milestone-107.json`, `native107-host-tests.log`, and `dsp-bringup` (query
oracle and sanitizer/Python logs). The recorded window at 4 seconds is already
back at the Vita3K library. The application was stopped after capture.

For a fresh private build, use the existing host/quad/audio preparation, prepared
DSP asset and shader contract, then select:

```sh
make -C games/halo2_5849 -j4 HOST_CHANNEL=1 QUAD_RENDER=1 AUDIO_HOST=1 AUDIO_DSP=1 AUDIO_MULTIBIN_UNAVAILABLE=1 GUEST_OPT=-O0 GENERATED=/private/h2/generated BUILD=/private/h2/build IMAGE=/private/h2/halo2_image.bin QUAD_SHADERS=/private/quad/prepared DSP_ASSET=/private/dsp/halo2-dsp.bin
```

This diagnostic package embeds owned executable and DSP code and must not be
uploaded or distributed. In this session, replay the exact archived build with
`python3 run_lab.py 107-replay native-107-artifacts/halo2-boot.vpk` from the private
directory, using only the owned `:111` lab. Preserve the previous trace first.
The next bounded task is the observed deferred listener-position update and its
original caller's following listener operations; spatial processing is still
unsupported until a real supported voice route exists.
