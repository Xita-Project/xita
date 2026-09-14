# Halo 2 first movie Play and real output cursors

Native104 passes the original first Play and reaches recurring movie processing.
The real mixer/output sink accepts nonzero game PCM, and original Bink callers
observe advancing source positions. Five original textured quad submissions and
six presented frames still produce only black pixels. There is no visible movie,
main menu or gameplay result, and no physical speaker audibility was tested.

## Supported contract

`AUDIO_HOST=1` adds fingerprinted public Play `37B6DF` (36 bytes, SHA-256
`a17acc34f95c42184adf9b8358685c5786d376096e3e001960cd3ea673e84cdf`)
and GetCurrentPosition `37B777` (32 bytes,
`480268179bb41c9501b344e0d150b1615af698de767221586e6e6aa73c1207ad`).
The guest ABI is respectively four arguments/RET16 and three arguments/RET12.
The existing mapped guest buffer, ownership, stack and output-alias checks run
before resource mutation. Guest return handling preserves native FPSCR.

Play supports only the first start of the sole bound PCM16 stereo 44.1 kHz
buffer, reserved arguments zero and flags 1 (looping). It starts the existing
real mixer voice. Repeated Play, other flags, outstanding Lock, Stop, restarting
and final Release of an active voice remain explicit unsupported boundaries.
There are no shared Halo CE mixer changes. Frequency remains native/44.1 kHz;
previously supported volume and headroom still affect the real PCM mix.

The output worker keeps one 1,024-frame grain outstanding. It observes the real
MAIN-port remaining sample count before queuing another grain; remaining counts
must decrease monotonically within the submitted grain. The source play cursor
uses consumed output frames and the mixer's actual 16.16 resampling step, wraps
at the source ring size, and rounds to whole stereo frames. The write cursor is
independently the real mixer's decoder read frontier. An earlier silent grain
submitted before Play is never credited to the new voice. Neither cursor is
advanced using elapsed wall-clock time or invented device interrupts.

The pinned Vita3K MAIN-port implementation clamps remaining samples to one
configured grain; its null Output call returns immediately. This is why the
worker explicitly observes the outstanding grain before teardown, instead of
assuming a null submission drains it. Negative, increasing or stalled counts
produce a strict backend error; an unverified drain retains its port/storage
for a later close retry. The one-second stall deadline detects failure, and
never manufactures progress. See the pinned
[SceAudio implementation](https://raw.githubusercontent.com/Vita3K/Vita3K/496939b6/vita3k/modules/SceAudio/SceAudio.cpp).

This cursor describes host driver consumption, not the instant a DAC emits a
sample. The private original-code oracle establishes nullable public outputs,
stopped positions, active wrapping and the Xbox hardware's 32-frame lead
conversion. The host read frontier replaces that hardware latency with the
actual software decoder frontier; Xbox sample-exact timing is not claimed.
The new adapter currently accepts cursor queries only after supported Play.

## Validation

All 32 host executables, the additional channel timing/active modes, and 42
focused Python checks pass. The PCM ABI test uses the unchanged real mixer,
including twelve mixed grains, actual decoded positions and `frames_out`.
The worker tests use concurrent threads and sink observations, exercising
one-outstanding-grain ownership, monotonic consumption, pre-Play silence,
creation rollback, failed joins, invalid/stalled remaining counts, and retained
resources until a verified drain. The pure progress test checks every remaining
count across 160 grains, ring wraps, counter overflow and rejection without
mutation. The ABI, concurrent worker and pure progress tests each pass ASan/UBSan.

Private execution of owned original x86 bytes passes two public Play wrapper
HRESULT/argument cases and 40 cursor cases. Critical sections and APU cursor
reads use controlled oracle fixtures; this does not emulate or validate the
Xbox hardware Play implementation. Private evidence is in
`../private/audio-host/movie-play-cursor-original-check.json`; the unchanged
input XBE SHA-256 is
`03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d`.

## Native104

The VitaSDK build and isolated Vita3K replay pass first Play at return `3E35DB`.
Observed cursors include `0 -> 308 -> 17236` bytes, with independent decoder
frontiers `0 -> 3840 -> 18944`. There are 59 cursor queries and seven matched
Lock/Unlock commits totaling 393,560 bytes, including a wrapped two-span commit.
The real sink accepts 145 grains, 29 nonzero, with peak magnitude 214 and no
backend error. These are accepted mixer samples; the lab uses SDL dummy output
and its UI remains muted, so physical audibility is not asserted.

Original movie update `155F80`, frame processing `1568D0`, five GXM quad draws
and recurring flips now execute. The last-presented snapshot has frame counter 6,
960x544 dimensions and exactly 522,240 pixels of `FF000000`. The SHA differs from
the previous black snapshot because its header frame counter changed. There
are still no visible original movie or menu pixels. The first four-second
window capture and decoded last-presented image are also black.

The next strict stop is original DSOUND entry `37B703`, return `3E38A1`,
ESP `005E5AF0`, with buffer interface `00B8601C`. No success is supplied at this
boundary. Terminal worker/port close returns zero and all snapshots complete.
The emulator log also contains an earlier host SIGSEGV notice at `09:35:26.938`;
original channel, vblank and audio processing demonstrably continues afterward.
Its cause is not established by this audio milestone. The known host fault at
`2008C8` during terminal shutdown follows the captured guest stop. The owned
emulator process is stopped after archiving.

| Native104 artifact | SHA-256 |
|---|---|
| ELF | `29f2d4d2e021a35140276e36759ef5b5390a664e27cf4e61fccd130f5f38fdf2` |
| EBOOT | `9d85f6d29730a8db644b52bbaa3f65cf559643d1cf488171827b23ce2e86b73a` |
| VPK | `c72353cd224826a025c09156f8bcaa1c936816ffd00ed56737a426cca86b1b32` |
| Boot trace | `2b05eca0f79c344aa125c2e78281a83a1eef285890cc94bb7254fe11ab9f7c46` |
| Channel JSON | `06bd05aff487c143d96f5f483de6237f835df9af50b1d10ab4e38c090be17f80` |
| Last presented | `882625e3d540275775e1df3ef98ee9139543454b620696b36129dc0ec431e1e5` |

Frozen artifacts are in `../private/native-104-artifacts`, captures in
`native-104-view`, and the exact build/generated source in `audio-movie-play`.
From `../private`, replay with `python3 run_lab.py replay104
native-104-artifacts/halo2-boot.vpk`; stop only that lab with
`python3 run_lab.py stop`. The diagnostic package embeds owned game code/image
and must not be uploaded as a distributable release.

The next task is to audit the original `37B703` stop/lifetime path and support
its real mixer/output behavior where proven. Then continue original movie
completion and the [normal skip path](halo2-movie-skip-ordering.md). The explicit
[effects-unavailable diagnostic](halo2-effects-image-audit.md) still leaves the
separate game sound initializer incomplete; it is not proof of a sound-free menu.

The [native105 Stop/rewind milestone](halo2-movie-stop-rewind.md) now supports
the observed cleanup sequence and records an actual guest Start press. It still
presents only black pixels and stops at a separately guarded device Release.
