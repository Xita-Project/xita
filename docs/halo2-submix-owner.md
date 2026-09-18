# Halo 2: inactive 3D submix ownership

Native111 reaches original `220A70` after creating all 82 empty stream voices.
Its `37D4BE` call returns to `220AC8` and requests a 24-byte descriptor with
`MIXIN | CTRL3D` (`2010`), no sample bytes, no format, no routing list and no
aggregation. This is not the movie's stereo PCM buffer.

Original `37CEF1` chooses the built-in mono 48 kHz, 24-bit-in-32-bit bus format
and sets input bin 31 for `MIXIN`. `380900` establishes exactly one physical
voice. The allocation/configuration path through `382F58`, `382CD4` and
`381AA9` configures that voice; `382CD4` skips ordinary sample binding for
`MIXIN`. This creation path does not start playback. The distinction is also
visible in the pinned [xemu multipass consumer](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/vp/vp.c):
it reads a mix bus, separately from sample-buffer decoding.

The H2 adapter now owns an inactive typed submix resource: one guest allocation
contains its opaque common header and a separate page with space for 32 mono
signed-24 samples in 32-bit containers. It retains one device reference and
releases both pages on final buffer Release. It never obtains a shared PCM
mixer voice or sends this descriptor through the permissive WAVE parser.
Creation failure publishes no output. Its bus is initialized to silence, with
zero headroom and original default routes `6,8,7,9,10`, all at zero dB.

This is ownership and pending state, not an active spatial mixer. The resource
has no packet attachment, sample decoding, DSP/HRTF scheduling or sink cursor.
Buffer data, Lock/Unlock, PCM controls, Play, Stop, status, cursor and immediate
listener commit reject it before changing state. Unknown original DSOUND
methods retain their existing strict guard. No zero-filled data is submitted
as a replacement for original audio.

The original initializer reselects the same five-bin route, then makes these
deferred calls. Each checked adapter accepts only the observed caller, value
and `apply=1`, preserving the exact integer IEEE payload and dirty bits.

| Public entry | Parameter | Value bits | Return address | Parameter offset / dirty bit |
|---|---|---|---|---|
| `37C620` | Maximum distance | `7F7FFFFF` | `220B3E` | `3C` / `00200000` |
| `37C644` | Minimum distance | `7F7FFFFF` | `220B4D` | `38` / `00200000` |
| `37C6C1` | Rolloff factor | `00000000` | `220B58` | `48` / `01000000` |
| `37C69D` | Doppler factor | `00000000` | `220B63` | `4C` / `02000000` |
| `37C600` | Cone outside volume | `00000000` | `220B6E` | `34` / `00100000` |

The offsets are into the original pending 3D parameter block, not the opaque
public host object. Only the selected fields/defaults are represented; derived
geometry and hardware state are not fabricated. Every wrapper has a verified
owned-XBE fingerprint and remains exclusive to the opt-in H2 audio profile.

The host suite checks bus precision/storage ownership, full guest context and
native FPSCR preservation, output and physical-page aliases, constructor
rollback, references, exact pending writes and strict unsupported calls. A
private 30-case original-code oracle executes the public wrappers and internal
setters without replacing any function, kernel call or hardware access. It
uses the original IRQL-2 no-lock branch, `shutdown=0`, and deferred application;
all writes are restricted to the expected parameter block and stack.

Separate private probes execute original `37CA0A` geometry, `37AE57` gain/filter
calculation and `380B97` attenuation packing without replacements. For the
observed listener/initializer values, six axis/zero position fixtures produce
packed words `FFFF000F` three times: front and effect-send attenuation zero,
back routes muted. Azimuth state changes separately. This does not validate
HRTF filtering, immediate commit or actual sound output. Those probes prepare
a future translation boundary; they are not runtime replacements in this commit.

Owned XBE/DSP assets, oracle programs, generated C, native packages and traces
remain private. Native106 remains the visible original Microsoft intro
checkpoint. No main-menu or gameplay rendering is claimed by this change.

Native112 completes all 51 original submix constructors and five deferred calls
per object. It then executes `GetEffectData` at caller `191456`, effect 1,
offset 0, 60 bytes, reading the initialized interpreter state. The next strict
stop is `37D4BE`, return `220C26`, ESP `005E5E1C`: descriptor `005E5E50` requests
flags `00100000`, no sample bytes/format/routing list, and input bin 13. Output
`00733E00` is untouched. Device references are 134 (public device, 82 stream
owners, 51 submix owners). The next task is auditing this distinct effects-bus
buffer and its original setup, not broadening the PCM descriptor checks.

Only black frame 1 is displayed, with zero nonzero audio grains. Worker close
returns zero and terminal snapshots complete. Vita3K subsequently hits its
previous application-teardown SIGSEGV/access `2008C8`, after the guest's strict
stop; no H2 emulator remains running. Validation passes all 36 host executables
and additional channel modes, the submix ASan/UBSan fixture, 26 focused Python
checks, and the 30-case original setter oracle.

| Private native112 artifact | SHA-256 |
|---|---|
| ELF | `62983eaeaf11b9ad9e9627c1bf984fdb975cf4f6159d1e0a961837541eaa10da` |
| EBOOT | `f59e04bdaf392d22394536fd4029fd7b4647fa195edc261e942ab4e42bd11b0d` |
| VPK | `a74b57d780d7c776db1a9cbc453dd4400dfb595a6a04a42215618fdd4598809f` |
| Boot trace | `4d0c37d242b686ad8267972629a98154efec3ed46c3f0d56d9c8c2b9e011fadc` |
| Last presented frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence is under `../private/native-112-artifacts`, `native-112-view`,
`native-milestone-112.json`, `native112-host-tests.log`, and `audio-host`'s
`check_original_submix_setters.json`, `probe_original_3d_attenuation.json` and
`submix-san.log`. The [DSP build options](halo2-dsp-game-init.md) apply. Replay
the exact archived package from the private directory with
`python3 run_lab.py 112-replay native-112-artifacts/halo2-boot.vpk`, using only
the owned `:111` lab and preserving prior traces. These diagnostic packages
embed owned game/DSP code and must not be uploaded or distributed.
