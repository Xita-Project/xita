# Halo 2 deferred Doppler and original HRTF configuration

This opt-in `AUDIO_HOST=1` increment extends the real-output device from
`140668f`. It stores the verified listener Doppler setting and executes the
original four-channel HRTF configuration writer. It does not implement 3D
voices, HRTF processing, DSP effects, buffers or streams. Unknown original
DSOUND methods still stop before accessing the host device's deliberately
incomplete XDK representation. The shared mixer, Vita output worker, CE paths
and default build are unchanged.

## Original evidence and corrected identity

The owned XBE SHA-256 is
`03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d`.
Native91 stopped at `37D52A`, return `21E5FF`, with factor bits zero and
apply=1. Its 36-byte public wrapper takes `(interface, float_bits, apply)` and
returns with `RET 12`; the 95-byte internal body at `37D2E5` writes listener
`+70`, ORs dirty bit `20`, and calls `37CD15` only for immediate application.
That commit path recalculates dependent state and clears the dirty field.
The original constructor's constant at `385824` gives the default factor 1.0.

The adapter preserves factor bits, default 1.0, pending versus active values,
and the dirty bit. A deferred call leaves active state intact; any supported
immediate listener setter commits all pending listener scalars. Valid factors
are 0..10, including signed zero, with apply 0 or 1. Invalid, nonfinite and
unknown-apply inputs stop without mutation. The original shutdown flag must
be mapped and zero; shutdown handling is outside this subset. These documented
factor bounds and deferred/immediate meanings are corroborated by Microsoft's
[SetDopplerFactor contract](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418024(v=vs.85))
and [Doppler factor limits](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416812(v=vs.85)).
There are no supported 3D voices to recalculate, and no frequency-shift output
is claimed by storing this listener setting.

The earlier description of the following `379F5B` call as DirectSound work was
incorrect. It is **DirectSoundUseLightHRTF4Channel**, with no arguments. The
31-byte wrapper calls the 30-byte IRQL/critical-section helper `379E9E`, then
`37E126`, then balances the lock if taken. The 111-byte `37E126` function writes
only ten algorithm pointers and mode=2 into the 44-byte table at `3871C8`.
The signature shapes, mode and wrapper cross-reference match the pinned
[XbSymbolDatabase HRTF selectors](https://github.com/Cxbx-Reloaded/XbSymbolDatabase/blob/20eced544726f5558c5a408458f38a086cc4e543/src/OOVPADatabase/DSound/5344.inl).
This identifies configuration, not sound work or audio processing.

The original wrapper/helper/writer execute their generated instructions; no
replacement writes the table or supplies a completion return. The original
kernel critical-section imports use the existing shared implementation. The
runtime guard checks the observed wrapper return `21E604`, nested helper
return `379F61`, writer return `379F69`, live device/output, mapped FS IRQL
byte, writable stack reserve, critical section and table. Physical overlaps
between these regions and aliases of the owned device page are rejected.
The read-only guard preserves guest CPU state, host FPSCR and guest memory.
All selected HRTF processing bodies remain guarded as unsupported.

Each new boundary is fingerprinted before code generation:

| Entry | Bytes | SHA-256 |
| --- | --- | --- |
| `37D52A` Doppler public wrapper | 36 | `ae868f50868faf0f465131b3bbd5e9535658ab3b345614c5c4274ce28e5d8166` |
| `379F5B` original HRTF selector | 31 | `7f397c78fa9ddaa3fc01f85707ae08b470294586a72dacef5c6dec704dea2f73` |
| `379E9E` original lock helper | 30 | `c66d5fef481a4aa92bb4e6df72e3f1e2cfb1fc0a9f0bc0ac8ff03aeaf34c597c` |
| `37E126` original table writer | 111 | `18c533b0df59f16b85abb24c514759e0eb47802cf4dd639bc51389c711aec77c` |

The separately audited internal Doppler body is 95 bytes, SHA-256
`7a8723c2a479ba00bea926ef8e311160a308e8dd06fe8283305423e0476e407a`.
Its private differential execution covers 192 combinations of valid factor
bits, application mode, existing dirty bits and IRQL, checking exact listener
writes, return ABI and preserved ESI. Four more cases execute the complete
original HRTF wrapper/helper/writer with different IRQLs and verify the exact
table footprint. The kernel lock calls are isolated by the test harness;
immediate 3D commit delegation is recorded, not emulated. This is not a test
of hardware HRTF/APU processing. The script and owned-code evidence remain at
`../private/audio-host/check_original_doppler_hrtf.py` and
`doppler-hrtf-original-check.json`.

## Validation

All 28 host executables plus timed/active channel modes and 38 focused Python
tests pass. The changed audio-device test also passes ASan/UBSan. Added tests
cover deferred zero versus active 1.0, all three immediate scalar setters,
signed zero/subnormal/boundary bits, reset, invalid/shutdown rejection,
full CPU/FPSCR and memory preservation, wrong original callers, missing stack
reserve, unmapped or overflowing FS, output/device aliases and distinct guest
pages with overlapping physical spans. Selected HRTF and effects-loader
methods remain explicit rejections.

The real mixer/output and bin-gain evidence from
[the previous native PCM probe](halo2-mix-bin-headroom.md) remains applicable to
those unchanged modules. This increment does not claim new nonzero game audio.

## Native92 result and exact replay

The original game calls Doppler with factor bits `00000000`, apply=1. Active
Doppler remains `3F800000`; pending Doppler is zero and dirty=`20`, as required
for a deferred update. Original HRTF selection runs at IRQL zero, invokes the
actual shared `RtlEnterCriticalSection` and `RtlLeaveCriticalSection` imports,
and installs the ten expected pointers plus mode=2. At the next stop,
LockCount=`FFFFFFFF`, recursion=0 and owner=0 confirm the lock was released.

The original section lookup/load then completes for `DSPImage`. Startup stops
at unsupported `XAudioDownloadEffectsImage`, entry `37B86D`, return `1913A9`,
ESP=`005E5ED8`, before that original method can dereference the opaque host
device. The four stack arguments are:

| Argument | Native value |
| --- | --- |
| Image name | `00453D28` (`DSPImage`) |
| Image-location pointer | `005E5EFC` |
| Flags | `00000001` |
| Output-descriptor pointer | `00734294` |

The real worker is healthy: one accepted silent grain, nonzero grains=0,
peak=0, error=0; terminal worker/port close returns 0. GET=PUT=`03B43280`,
packet remainder zero. Stored frame1 is 960×544 with every one of its 522,240
pixels `FF000000`. The display capture remains black, with no movie/menu or
gameplay; the application returns to Vita3K idle and its process is then
stopped. This remains the earlier sound success path, not native87's later
no-driver mainmenu-map path.

Frozen evidence is `../private/native-92-artifacts`, `native-92-view` and
`native-milestone-92.json`. The first-frame/channel/device/push hashes match
native91; only startup has progressed.

| Artifact | SHA-256 |
| --- | --- |
| ELF | `0d1a4025a8727744b7b82ac9232f1a9e9a75e8df60fbea319cf3956f11f6696d` |
| EBOOT | `1e911a36f5941142493afd954f6be66020d35937c7c44a52d11194dd3c4cc893` |
| VPK | `a7bb706655b25694f2089f928a0080f54fcccde61249ccf4fe17f993a3e7af0e` |
| Trace | `3ad04b8346d0fb74b2fe63bdc415876bc4d9bc11d5cc9933315e8595765be4b3` |
| Channel JSON | `a2b3a19912c99b5a3e79150d46afd479d4b945b4679f2a369fa6ec0f4655bf00` |
| Stored first frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Generation and build use the [host-audio instructions](halo2-host-audio-device.md)
with a new private `audio-doppler` directory, `HOST_CHANNEL=1 AUDIO_HOST=1
QUAD_RENDER=1`, `GUEST_OPT=-O0` and four build jobs. Automatic discovery remains
11,764 candidate functions, 161,187 basic blocks, 1,121,588 instructions and
3,832 unsupported occurrences; these are not execution coverage.

For the exact archived executable, from the existing `../private` directory:

```sh
python3 run_lab.py doppler92-review native-92-artifacts/halo2-boot.vpk
```

No input or cache reset is needed. Collect `vita3k/ux0/data/xita-halo2/boot.log`
and stop the isolated instance with `python3 run_lab.py stop`. The helper uses
only title `XH2B00001`, its own configuration/data roots and display `:111`.
The package embeds owned game image/code and must not be uploaded as a
redistributable release. Assets, generated C, packages and traces remain
private/outside Git. No CE, physical Vita or USB files were touched.

## Next bounded boundary

Audit the actual `DSPImage` descriptor, relocation and effects-data consumers
before choosing an implementation. The section is present in the owned XBE
(28,768 bytes). The original `37B86D` loader has four arguments and `RET 16`;
its complete 514-byte body has SHA-256
`152b5769f3630d38073a25543bbce4d35f387ad3af712ad1b96f5427794f2b67`.
The caller at `191300` uses the returned description and then requests effect
data for indices 4..7. No useful no-effects success contract has been proved.
The next task is a read-only audit of those exact requirements and failure
control flow, followed by a real supported effect route if feasible. Returning
a fabricated descriptor or swallowing the failure is not part of this work.

The follow-up [effects-image audit](halo2-effects-image-audit.md) records the
original DSP requirements and the explicit failure-path experiment through
native95. It does not supply DSP success or a visible main menu.
