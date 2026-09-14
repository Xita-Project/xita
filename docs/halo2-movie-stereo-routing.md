# Halo 2 movie stereo routing

Native99 reaches an explicit two-bin FL/FR request after the first paired
movie-buffer write. This increment accepts only the real mixer's existing
stereo route and unity per-bin gains. The six-speaker request remains an
explicit diagnostic failure; no surround processing or guessed downmix is
introduced. Guest playback/cursor methods remain unsupported.

## Original evidence and supported contract

SetMixBins `37C5E4` takes the public buffer interface and a list pointer, with
`RET 8`. The list contains a count and pointer to `(DWORD bin, LONG volume)`
pairs. The original `37BC89` stores ordered bin IDs at settings `+28`, indexed
volumes at `+30 + bin*4`, and the count at `+24`. The original PCM constructor's
default stereo list is exactly `(0,0),(1,0)`. Private execution checks verify
that an explicit two-bin call selects that state and that a repeated call is
idempotent. These extend `../private/audio-host/movie-routing-original-check.json`.
The critical-section, pitch and hardware commit helpers are isolated in that
oracle; original settings parsing and public route storage execute.

Native100 accepts that actual FL/FR route twice and reaches SetMixBinVolumes
`37B6C3`, return `3E3258`, ESP `005E5D44`. Native101 adds only a bounded read-only
terminal list capture. It observes a list at `005E5D60`, count 2, pairs at
`005E5D68`: `(bin 0, volume 0),(bin 1, volume 0)`. Both calls occur through the
original Bink control helper. Neither run reaches guest playback.

SetMixBinVolumes uses the same two-argument list ABI and `RET 8`. Its public
wrapper reaches `37B282`, then `37A64C`, then `37A4DB`. The last function writes
only the requested indexed gains; the subsequent `381CE4` commits gains to
Xbox hardware. The private original-code check uses three different prior
gain states and proves that selecting unity changes only those two gain
words. It isolates the critical-section and hardware commit helpers, and
checks public return/stack behavior. Evidence:
`../private/audio-host/movie-bin-volumes-original-check.json`.

| Complete function | Bytes | SHA-256 |
|---|---:|---|
| `37B6C3` public SetMixBinVolumes | 28 | `73830059a6d6dbf64dbb0994e2515c1af2c9b482e8bdd44f82cd206d1191e614` |
| `37B282` guarded wrapper | 78 | `da8566042b9f4ea8c31cef439ec77136892bca9f5eb226d657e91b16271dd0d1` |
| `37A64C` settings/commit call | 29 | `ddbebc2b0b11a5dfa7054d92e7b0ba646d4def54edd4ccda0ff9ed710c01c863` |
| `37A4DB` indexed gain storage | 37 | `a3935caec3051fd4e87e9c3ff82b7dc0442f311d192dfd966acebfa9837a3164` |

The adapter requires a live, bound stereo PCM buffer and a fully mapped list
with exactly ordered bins 0 and 1, both volume 0. The actual shared mixer
already sends source channel 0 to left and channel 1 to right at unity per-bin
gain. Selecting that existing state therefore needs no resource or mixer
mutation. The separate buffer volume/headroom and device bin headroom retain
their real effects. Null/default lists, other bin counts, reordered/duplicate
bins and nonzero per-bin gain requests stop explicitly. The additional
SetMixBinVolumes hook is fingerprinted and available only with `AUDIO_HOST=1`.

`AUDIO_MULTIBIN_UNAVAILABLE=1` retains its narrower behavior only for
SetMixBins and only the observed six-bin caller/list: return the genuine
unsupported HRESULT without changing the existing stereo voice. It never
turns a six-bin gain request into success or a guessed gain approximation.

## Validation and retained evidence

All 31 host executables plus timed/active channel modes and 42 Python tests
pass, including default and diagnostic builds. The actual shared-mixer buffer
test also passes ASan/UBSan. It verifies full guest CPU/FPSCR return state,
unchanged guest memory/object/voice/resource state on accepted idempotent calls,
and rejection before mutation for unmapped/cross-page lists, malformed counts,
duplicate/reordered bins, unsupported gains, failed backend and device shutdown.
Both stereo APIs run before and during test-only mixer playback, whose samples
confirm independent left/right routing and retained volume/headroom attenuation.
These test-only Play calls do not implement the guest Play API.

Native100 and native101 each retain one 106,496-byte commit, four silent output
grains, and only the black initial framebuffer. Their raw frame SHA-256 is
`a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1`;
the complete channel JSON is
`a2b3a19912c99b5a3e79150d46afd479d4b945b4679f2a369fa6ec0f4655bf00`,
GET/PUT `03B43280`. Native100's later capture is the emulator library after
the checked guest exit. No original movie/menu content is displayed.

| Build | ELF SHA-256 | EBOOT SHA-256 |
|---|---|---|
| Native100 stereo route | `f024d40d6ba29f451cf82aade47059c3ea5588183555aaf2783515f6078c1f2c` | `893fa87c3d4060a1143abd5e726a859c4a4fdd807529d089eaff0315b70308b9` |
| Native101 read-only gain capture | `b20e5eddb8049a95d991a53a2cec58dfeecb06f324cf9334f477b82f2853328e` | `62c5e7264e89bd028c7145f9a405b6e97b81629025a5965eb4b6ffdf2cc719a9` |

Immutable private evidence is in `../private/native-100-artifacts/`,
`native-101-artifacts/`, corresponding `native-N-view/` captures and
`native-milestone-N.json` manifests. Owned executable bytes, disassembly,
generated code, samples, packages and traces stay outside Git. Diagnostic VPKs
embed owned game code/image and must not be uploaded as distributable releases.

## Native102: original movie writes and first Play request

Native102 accepts both stereo APIs, runs original Bink processing and completes
a second whole-buffer Lock/Unlock pair. Lock returns to `3E3578`; Unlock returns
to `3E36C4`. Total committed bytes are now 212,992 across two commits, with no
outstanding lock. This records writes, not decoded-audio correctness.

The next strict stop is public Play `37B6DF`, return `3E35DB`, ESP `005E5D38`.
Its arguments are buffer `00B8601C`, reserved words 0 and 0, and flags 1 (loop).
No Play success is supplied. The real sink has accepted 88 silent grains,
with no output error; the frame and channel hashes remain the same black frame
and empty second-device channel recorded above. The 4-second desktop capture
is black. No original movie or menu is visible and no nonzero game audio has
been produced. Terminal worker/port close returns 0 and all snapshots complete.

| Native102 artifact | SHA-256 |
|---|---|
| ELF | `86db8612dd77b7cee2d096892a012561bb44052d5d61c0cf53374fc3ba890757` |
| EBOOT | `516780260e1a07d3dab333750765df6380ef9dc64c2f1ed8e1aea8b77490ba02` |
| VPK | `572939b13cba93c02081b5a235cf7ce854304c4490905dfade107979b6e0787b` |
| Boot trace | `28278bd70feb9493b57b0afea071f63613727c1c580ea5dd85a4fafa03d8bf74` |

The fresh generated-source/image/build directory is
`../private/audio-movie-stereo/`. All earlier build flags remain:
`HOST_CHANNEL=1 QUAD_RENDER=1 AUDIO_HOST=1 AUDIO_EFFECTS_UNAVAILABLE=1
AUDIO_MULTIBIN_UNAVAILABLE=1 GUEST_OPT=-O0`. Replay the immutable package only
in the isolated lab:

```sh
cd /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private
python3 run_lab.py replay102 native-102-artifacts/halo2-boot.vpk
# After the terminal trace and snapshots complete:
python3 run_lab.py stop
```

The next investigation is the original input/skip path and its ordering against
this first Play request. If playback is required before input can be processed,
the cursor must derive from real mixer/sink progress. The mixer's decode-ahead
byte position must not be substituted for an actual playback position.
