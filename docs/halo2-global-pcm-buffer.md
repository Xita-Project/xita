# Halo 2: original global low-rate PCM buffer creation

Native129 creates the first original mono8/1000Hz buffer, binds its external
1000-byte region, removes headroom and applies volume-10000. It then stops at
Play because loaded-DSP PCM routing is still unsupported. **The native frame
remains black, nonzero game audio remains zero, and no main menu or gameplay
is demonstrated.** This follows the [fifteen-source loop](halo2-fxin2-extra-sources.md).

Native128 first captured the global descriptor without changing execution:
`37D7DE`, return `22153E`, descriptor `005E5EEC`, output `005E5EC0`.
Fields are size24, flags0, bytes0, format `005E5ED8`, mix list `005E5EC8`,
input-bin0. The format is PCM tag1, mono, rate/average1000, alignment1, bits8,
no extension. The single route is bin14 with volume0. The caller `221490`
subsequently binds1000 bytes, removes headroom, mutes and loops this buffer;
its parent invokes the same setup twice.

The original public wrapper takes descriptor/output and returns with `ret8`.
It acquires the singleton device through `37D72A`/`37B16F`, calls internal
creation `37D159`, then releases the temporary device reference. Private
success/failure audits execute110 original instruction addresses, isolating
only internal buffer creation to its checked result and child reference.
Success retains one child-owned device reference; failure leaves output and
net reference count unchanged. Wrapper87-byte SHA-256:
`fba5b0697871b05f1c503ba189363fbceb37cc38b63e1a4dd0b427a660cb35be`.

A separate original parameter/voice audit executes928 instruction addresses
for this PCM descriptor and its headroom/mute controls, isolating allocation
and FIFO capacity. It records the nominal1000Hz format, pitch-22876, default
600-hundredths-of-dB headroom, route14, headroom0 and final volume-10000.
The latter writes all-FFF attenuation fields. This is command-generation
and state evidence, not MCPX output validation.

The adapter validates the fingerprinted caller, descriptor, full format,
route, fifteen active FX predecessors, object sequence and mapped/unaliasing
inputs before allocation. It owns a real shared-mixer PCM voice plus its
buffer/mirror and child reference. The shared format parser has a4kHz minimum;
the existing frequency override sets this voice's effective rate to1000Hz
without changing shared/CE parsing. The shared decoder uses its existing
16.16 resampling model; exact hardware pitch quantization is not claimed.

Data binding and the two controls are restricted to their observed callers
and values. The global creation path permits at most two such objects and
requires its predecessor to have played before the next creation. Other
methods, unmuting, frequency changes and loaded-DSP playback remain strict
stops. No placeholder sound object or invented successful Play is supplied.

All40 host executables and28 focused Python tests pass. The owned-coefficient
ABI fixture covers full context, cross-page unaligned inputs, each changed
format byte/descriptor field, aliases, allocation failure, unchanged failure
output, real voice allocation and inactive lifetime. ABI ASan/UBSan passes.
A host decoder check feeds synthetic8-bit samples at the effective rate:
unmuted test input produces nonzero samples, while volume-10000 produces zero
samples and still advances the real decoder/output-frame counters. This
host-only check does not establish native PCM/DSP coexistence.

Native129's original calls create interface `0136601C` (mixer voice82), bind
external `007343AC` to mirror `01376000`, then apply headroom0 and mute.
The next strict stop is `37B6DF`, return `2215B9`, ESP `005E5E9C`.
Parent references are150; all fifteen FX bits remain active. Computed /
submitted / consumed frames are25,600 /25,600 /24,576; total compute1,530,887us,
maximum grain68,368us. Error, peak and nonzero grains are zero. Close and
snapshots complete. The real DSP worker remains slower than real time.

| Artifact | Native128 SHA-256 | Native129 SHA-256 |
|---|---|---|
| ELF | `1d42f7d6d84a18e5ddde6786127a87d15e4f2529cecdfaaf7619a5ed06b7ebfa` | `14dab75a7b07fe5e5b63cd82446094e674275982211b2c57ca08f24094b23a2f` |
| EBOOT | `0be21d663c3ac378c3be0082b2dfaa53e76e897c19a98b9b5e155c5cf8f04f45` | `f7c6596be412d7d44082b597741c49430f5576eb44003c0029c4e685f04b555e` |
| VPK | `15e53afc295664f4c57a7c55b07ce49a9cadaf485ef16a32869fd0a80d936112` | `d0eda1d163f52127971eb1083fa8db47e3102d726cfe7a2a2ebb0ab78937b9e0` |
| Guest trace | `aac0b07f8893a9e49d623363f6a58ba6654eabef5fab9133b79adf78def1e4d7` | `857d85e34e66dc4248d558c1a10e7ccd62331ffeafb276c3016c9fe3afd84d91` |

Both black scanouts have SHA-256
`a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1`.
Private evidence: native128/129 artifacts/views/manifests, `audio-global-trace`,
`audio-global-pcm`, `audio-host/global-buffer-wrapper-original.json`,
`global-pcm-params-original.json`, and `dsp-bringup/audio-global*`.
Regeneration changes only `code_106.c`;131 other files were compared before
cache reuse. All169 dependency targets were retargeted and verified.

The next task is a real, explicitly muted PCM-to-GP route: decode/advance the
owned source, verify zero contribution to bin14, and track actual submitted /
consumed grains for each voice. Prepared grains must retain their original
owner mask. Any nonzero or unsupported route/state must stop rather than be
silenced by the adapter.

From the private directory:

```sh
python3 run_lab.py 129-replay native-129-artifacts/halo2-boot.vpk
python3 run_lab.py stop
```

Build uses the same real DSP, spatial/filter and diagnostic multibin options
as native127 with `audio-global-pcm/generated`, image and build directory.
**Diagnostic packages embed owned game content and must not be uploaded or
distributed.**
