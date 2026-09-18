# Halo 2 movie PCM buffer boundary

The original Bink setup requests a PCM16 stereo 44.1 kHz buffer. The opt-in
`AUDIO_HOST` adapter now creates a real shared-mixer voice, retains its device,
and binds a private sample mirror to that voice. Guest playback, Lock/Unlock,
routing, streams and effects remain unsupported in this increment. Host sample
tests establish the real mixer connection; they do not establish movie playback
or a visible main menu.

The later [paired write-commit increment](halo2-movie-write-commits.md) adds
checked Lock/Unlock ownership after these captured boundaries.

## Original descriptor and ownership

Native96 stops at public `37D4BE`, return `3E3B97`. Its six-DWORD descriptor at
`005E59CC` is size=24, flags=`A0`, initial bytes=0, WAVEFORMATEX=`005E59B8`,
mix bins=0 and input bin=0. The format is tag=1, channels=2, rate=44100,
average bytes/second=176400, block alignment=4, sample bits=16, extra bytes=0.
Output is `8006403C`, aggregation is null. The owned XBE SHA-256 is
`03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d`.

The complete original Bink setup `3E3A10` is 629 bytes, SHA-256
`7249ec4479fd76b02bbc81c92e999c13ca94cfa5f0e1eb372575e78a4445edbe`.
It allocates external sample storage, calls CreateBuffer, then calls
`37CC4A` SetBufferData at return `3E3BBD`. The common buffer constructor
`37BE27` calls `37B16F`, which AddRefs the supplied parent device. The buffer
starts with one reference and the common vtable `417150`; public wrappers
use the allocation base plus `1C`. Common AddRef is shared with the device at
`37A14F`; common buffer Release is `37A795`.

Bink `3E39A0` stops the buffer, releases its public interface at return
`3E39C5`, and only then frees its external sample allocation through `36D420`.
The adapter therefore never frees the supplied source allocation. Last buffer
Release frees the mixer voice under its real lock before releasing mirror and
object storage, then releases the retained parent. Releasing the caller's
last device reference while a buffer remains keeps the output worker alive.
This is a synchronous host resource lifetime, without Xbox hardware deferred
voice destruction.

The original callback pair `3E3530`/`3E3670` brackets movie writes using
Lock/Unlock. The original Xbox Unlock at `379F40` is a five-byte success return;
it performs no copy. A later supported host playback path can use these audited
commit boundaries to update mixer-owned samples under its lock. This increment
only supports the first SetBufferData bind. It does not promise coherence for
arbitrary direct writes or live rebinding.

## Supported subset

CreateBuffer accepts only the complete observed description and exact 18-byte
PCM format above. Unknown codec tags, rates, channels, inconsistent averages,
block alignment, extensions, flags, initial data, mix inputs and aggregation
stop before allocating resources. It allocates a guest object page and a real
shared-mixer voice; allocation/voice exhaustion returns `8007000E` while leaving
the caller's output and parent references untouched.

SetBufferData accepts a first mapped, four-byte-aligned external region up to
one MiB, with a nonzero whole-frame byte count. It allocates and fills a private
mirror before binding the real voice. Output/source validation rejects physical
aliases of adapter-owned device, buffer and mirror pages. The source remains
caller-owned and is never passed to the worker. An unbound or stopped voice
produces no samples; no guest Play success is supplied.

The original settings constructor `37CDCA` initializes flags `A0` with 600
hundredths dB headroom and effective volume -600. Original SetVolume `37A5D4`
stores requested volume minus headroom; SetHeadroom `37A629` adjusts that same
stored attenuation while preserving the requested volume. A private original
execution check verifies this default, the parent AddRef and 24 setter cases,
with format/routing/hardware commit and critical-section helpers explicitly
isolated. These checks do not execute an APU.

The adapter supports frequency=0/native or 44100, volume in [-9000,0], and
headroom=0 or 600. It applies volume minus headroom to the actual mixer under
its lock. Other values stop. The [documented DirectSound volume unit](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/mt708939(v=vs.85))
is a hundredth of a decibel. The pinned [Cxbx voice bookkeeping](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/DSOUND/common/XbInternalDSVoice.cpp)
independently matches the headroom algebra. No broad Cxbx success fallback is
used. The shared mixer's existing 16.16 resampling, 8-bit gain quantization and
50-percent master level remain unchanged; this is not sample-exact Xbox pitch
or APU emulation.

Seven additional complete public/common boundaries and the two buffer
reference-vtable entries are fingerprinted in `hooks.py`. Original unknown
DSOUND methods still stop before accessing the deliberately incomplete host
object representation. The explicit `AUDIO_EFFECTS_UNAVAILABLE=1` diagnostic
remains separately required to reach this movie path: it reports a genuine
unsupported-effects HRESULT and preserves the game's failure cleanup. This is
not a proven usable no-effects menu mode.

## Validation and evidence

Thirty host executables plus timed/active channel modes and 41 Python tests
pass. The new buffer test uses the actual shared PCM mixer with synthetic guest
pages, including noncontiguous mappings and cross-page descriptor/format input.
It checks full return CPU/FPSCR state, malformed inputs without mutation, real
voice exhaustion and allocation rollback, protected physical aliases, caller
sample ownership, device/buffer reference ordering and release while a test
voice is playing. ASan/UBSan also passes.

Test-only direct mixer playback verifies nonzero stereo samples, volume and
headroom changes, isolation from uncommitted source writes and cessation on
release. It deliberately does not route guest Play through a success stub.
Native96's captured frame remains entirely black, SHA-256
`a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1`.
Its guest trace and channel snapshot complete before a Vita3K post-exit
Stopping/Idle crash; that shutdown fault remains unclassified.

Private audit inputs are under `../private/audio-host/movie-buffer-*` and
`check_original_buffer_controls.py`; native96 is frozen in
`../private/native-96-artifacts` and `../private/native-96-view`. Owned executable
bytes, disassembly, generated C, sample captures and game-embedded packages are
outside Git. Diagnostic VPKs embed owned game image/code and must not be uploaded
as distributable releases.

## Native97 and exact replay

Native97 successfully creates buffer interface `00B8601C` on mixer voice 0.
It binds external region `80E06D60`, 106,496 bytes, to private mirror `00B96000`.
The original Bink control calls set frequency 44100, volume 0 and headroom 0.
The retained parent at `00B76000` has two references. The next strict stop is
`37C5E4` SetMixBins, return `3E321F`, stack `005E5940`. Its list at `005E595C`
contains six entries at `005E5964`: bins 0,1,2,3,4,5, each with volume 0.
No route is accepted or changed at that stop.

The real output worker has accepted one silent grain: nonzero grains=0,
peak=0, error=0. Its terminal close succeeds. The captured original frame is
still frame 1, 960x544 with all 522,240 pixels `FF000000`, with the same raw
SHA-256 above. The decoded channel snapshot is unchanged, SHA-256
`a2b3a19912c99b5a3e79150d46afd479d4b945b4679f2a369fa6ec0f4655bf00`,
GET=PUT=`03B43280`. No original movie, menu or gameplay is visible. Vita3K again
faults at host address `2008C8` during post-exit shutdown, after all terminal
snapshots complete; the cause remains unclassified.

| Native97 artifact | SHA-256 |
| --- | --- |
| ELF | `f0a2c098d9e655f975e1dfd92957d6cdc601b4eec51027a2de2b8994b347109f` |
| EBOOT | `9eb7024bd9953b8dde76d2fbcc47b01673cb92e39f85d248768c1909eeba78b0` |
| VPK | `ecfced6e19eb57641b3ce39f680fb9812686f2e86ebc31b8c25c1e3f2359e39b` |
| Guest trace | `553958e921e850e4b99c18749969723a584af20bd44e44ec57a33bc557166cf7` |

The exact generated-source/image directory is `../private/audio-movie-buffer`.
The frozen executable, package and trace are in `../private/native-97-artifacts`;
captures are in `../private/native-97-view`. Source build flags are
`HOST_CHANNEL=1 QUAD_RENDER=1 AUDIO_HOST=1 AUDIO_EFFECTS_UNAVAILABLE=1 GUEST_OPT=-O0`,
with the existing private quad shaders and four build jobs. Replay the archived
package without regenerating or rebuilding:

```sh
cd /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private
python3 run_lab.py replay97 native-97-artifacts/halo2-boot.vpk
# After the terminal trace completes:
python3 run_lab.py stop
```

This uses only the isolated `:111` lab and its existing owned asset links. The
next bounded task is to establish the six-bin routing contract for this stereo
buffer, then implement verified write commits and playback/cursor behavior.
The shared mixer exposes a decode-ahead cursor, so it cannot be treated as an
accurate DirectSound play cursor without additional validation.

## Explicit unavailable-multibin diagnostic

The actual PCM format/default-settings path through `37CDCA`, `37C825`,
`37DA87`, `37D9EE` and `37BC89` selects the two-entry list at `3858BC`:
bin 0/volume 0, bin 1/volume 0. This matches the existing real stereo mixer.
The requested six-bin list is different. Original `37BC89` stores its ordered
bin bytes at settings+`28`, signed volume indexed by bin at +`30`, and count at
+`24`. No 3D/multipass transformation applies to the observed flags `A0`.

The pinned [xemu VP implementation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/vp/vp.c)
assigns each hardware routing slot its source channel modulo channel count.
Six alternating routes therefore cannot simply be treated as two routes by
silently dropping center, LFE and rear bins. Their final fold-down requires an
established speaker/DSP output policy. That policy is not implemented here.

With the separately enabled `AUDIO_MULTIBIN_UNAVAILABLE=1`, the exact movie
call returns the [documented unavailable-operation result](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416776(v=vs.85)),
`DSERR_UNSUPPORTED=80004001`, and leaves the
real default FL/FR voice unchanged. This is a failure-path diagnostic, not a
six-bin implementation. It requires the real audio host and checks caller
`3E321F`, a live bound buffer, mapped list and all six exact `(bin,volume)` pairs.
Unexpected callers, layouts, counts, values and invalid memory still stop.
Without this flag, even the observed valid six-bin request remains fatal.

Bink's original `3E321F..3E325D` return path does not test the SetMixBins HRESULT.
The caller at `3E3BF4` immediately invokes its normal clear-buffer helper.
Private original-code tests compare success and `80004001` and reach identical
Lock arguments at `37B7B3`, return `3E3293`: offset 0, size `1A000`, flags 0,
and the buffer's own pointer/length output fields. This establishes control
flow under the stated failure, without claiming that Xbox normally rejects
this valid six-bin list. Evidence is
`../private/audio-host/movie-routing-original-check.json` and its private
`check_original_movie_routing.py` driver. Hardware route commit, pitch conversion
and critical-section helpers are isolated in that test.

Thirty-one host executables and 42 Python tests pass for this increment. The
failure-enabled buffer executable also passes ASan/UBSan. Its full-state checks
verify no guest memory, buffer, mixer voice, device reference or allocation
changes on the accepted negative result. Both default strict and diagnostic
builds reject malformed callers, counts, route values and mappings. Subsequent
test-only real mixer playback retains the expected stereo samples.

Native98 executes the explicit failure and reaches the original whole-buffer
Lock at `37B7B3`, return `3E3293`, stack `005E596C`. Its arguments are
interface `00B8601C`, offset 0, length 106496, pointer1/length1 outputs
`8006408C`/`80064088`, pointer2/length2 outputs `80064094`/`80064090`, flags 0.
The worker again reports one silent grain, no nonzero grains and no output
error. The black raw frame and channel snapshot retain the hashes above;
no movie/menu is visible. Terminal snapshots complete before the same
unclassified post-exit Vita3K fault. The lab uses dummy/muted audio; host sample
checks do not imply physical audibility.

| Native98 artifact | SHA-256 |
| --- | --- |
| ELF | `d93347ab1eeb880c8bb72fb1e89c1024a967fece3b22e161a8915481c4b7c840` |
| EBOOT | `5ccce42470d09ee42d8fb5922fc2fd285f13bb2df8fe149dd37914e7987b142e` |
| VPK | `8a2ba336f08a9be79f4ab68acfe47e55d30545eddbb6694cecc2f56c29962bec` |
| Guest trace | `35f061e9d562027331bd0b6c2b7b12bce5584c67d7b9171e0eed5301f381b914` |

The frozen artifacts/captures are `../private/native-98-artifacts` and
`../private/native-98-view`. The fresh source/image/build directory is
`../private/audio-movie-stereo-failure`, with Native97's flags plus
`AUDIO_MULTIBIN_UNAVAILABLE=1`. Exact replay uses the same private launcher:

```sh
cd /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private
python3 run_lab.py replay98 native-98-artifacts/halo2-boot.vpk
# After the terminal trace completes:
python3 run_lab.py stop
```

The next boundary is the original Lock/Unlock pair and committed guest sample
ownership. No implementation of that pair, playback, cursor reporting or
surround fold-down is included in Native98.
