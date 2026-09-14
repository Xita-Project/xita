# Halo 2 original intro visible in Vita3K

Native106 visibly displays the original Microsoft Game Studios intro in the
isolated Vita3K window. It also processes a normal Start press, finishes original
movie cleanup and reaches main-menu map sound setup. The main menu does not
appear: the original call still traps on the null sound object left by the
explicit unsupported-effects failure.

## Audited cleanup and diagnostic changes

Original movie close `3E39A0..3E3A0A` is 107 bytes, SHA-256
`d4bb02dd206f80e2ce561f70fe31ded2d47a2c90cd76546ab8fd026c1b21d238`.
It stops and releases the buffer, frees the original sample allocation, decrements
the movie device count, and calls public device Release `379F2A` only for the last
owned device. Its return `3E39FD` is now accepted by the existing guarded original
wrapper, which adjusts the interface by eight and invokes the already tested
real Release adapter. The original caller then sets its device sentinel to
`FFFFFFFF`. Unknown callers, invalid interface/vtable and invalid stack mappings
still reject. No new success-only resource operation is supplied.

The private original-byte oracle executes this complete caller and the public
Release wrapper, observing Stop, buffer Release, sample free and common device
Release in that order. Resource operations are controlled fixtures; the real
adapter lifetime is separately host/native tested. The caller/vtable guard tests,
all 32 host executables and extra channel modes pass. The device guard/lifetime
suite passes ASan/UBSan; the focused Python audio-profile checks pass. The native
VitaSDK build passes without regenerating guest code.

Read-only diagnostics sample the original movie header before its first four
frame-processing calls and every sixtieth call. Validated guest reads preserve
FPSCR. At the same bounded intervals, the existing GXM backend counts nonblack
pixels in its already copied texture and completed target. It stores at most one
nonblack input/output pair privately. No guest memory, shader, texture, command,
rendering state, audio behavior or movie timing is replaced by these probes.
The enclosing draw hook preserves FPSCR across these diagnostic operations.

## Native106 evidence

Before this replay, the populated private cache4 directory and its raw image were
preserved under `../private/native106-prior-cache4`, with a file/hash manifest.
The game receives a new empty private cache4 directory. Its original FATX format
is validated and main-menu cache loading progresses; the prior populated-volume
raw-access rejection does not occur. Owned maps are untouched. A general FATX
driver is still absent.

The original Bink header reports 640x480, 556 frames, and rate 2997/100. Frame
number advances from 1 to 4 in the first four sampled calls, and later header
samples continue at invocations 60, 120, 180 and 240. At draw60, the texture and
completed GXM target each contain 307,200 nonblack pixels. Their complete pixel
arrays are byte-identical, and the actual emulator window displays the Microsoft
Game Studios logo. This is decoded original game content rendered through GXM,
not a stand-in image or launcher screen. No claim of full movie accuracy or speed
follows from this one sample.

Enter/Start is sent at epoch `1789397990.4929035` and released at
`1789397992.4931512`. The game observes `digital=0010`; original Bink cleanup
completes its worker exit, buffer release and last device Release. This run has
61 real Stops, 59 zero rewinds and 60 looping Play calls. The earlier
[native104/105 evidence](halo2-movie-stop-rewind.md) measures accepted nonzero PCM;
no physical speaker audibility is claimed for this muted SDL-dummy lab.

The main-menu map callback then reaches `21F6D0 -> 21E3B0`. Sound state `007317BC`
still has zero device and selected-object slots at `0073426C` and `00734270`:
the earlier explicit effects-unavailable failure aborted that initializer.
The original indirect call traps at target zero, return `21E3EA`,
ESP `005E5F44`. No object or successful effects descriptor is fabricated.
The final presented frame251 is black after movie cleanup. That final snapshot
alone would miss the earlier visible intro, which is preserved in the live capture.

| Native106 artifact | SHA-256 |
|---|---|
| ELF | `53ff6fad175c168fdf246481147416330f299906137b16d6346cc3c5edfa9af7` |
| EBOOT | `487d617da7e54d8688e02d2ae55a4b9a6cb7c8998b6a88cfe28c233418e37d74` |
| VPK | `d5b17ad564a5f0c9e4300a7b85d15dbc950cf07c0a8a533cb81794014a89e7fe` |
| Boot trace | `614702b274d2d09ddc6795a4a74ff98da6bb906eeb424fc9fe9c16a3c258b820` |
| Channel JSON | `cad9588e397e2209d6fd297ec9b1b0f0bd85078d7ae6a644e8ff83657830ba20` |
| Final black frame | `b402daa3c1fced6f4dae44f72b7b38a1667a21fc1a05958e93a5a70e6253d48e` |
| Draw60 input and GXM output, each | `f647614ea653ed33e828ecca1e7518207adc8c47a85b16147ab7f5074c7b5c62` |
| Actual visible window capture | `f295329247fe1e2621c6085ac2f1535916cbab247fadd106d7092d1df6376250` |

Frozen artifacts are in `../private/native-106-artifacts`; the actual window is
`native-106-view/window-live-60.png`, and original input/output pixels are
`movie-input-first.bin` and `movie-output-first.bin`. Build/generated source is
`audio-movie-stop`. From `../private`, preserve the previous cache4 directory/raw
image under a new unused private name and create an empty cache4 directory, then
run `python3 run_lab.py replay106 native-106-artifacts/halo2-boot.vpk`. Allow the
original movie/map loading to progress, capture the actual intro and send normal
Start. Stop only the owned lab with `python3 run_lab.py stop`.

All terminal snapshots completed and the owned emulator is stopped. The package
embeds owned game image/code and must not be uploaded as a distributable release.
Owned movie frames, generated source and traces remain private/outside Git.
The next necessary implementation is the real
[DSP/effect initialization dependency](halo2-effects-image-audit.md) required by
main-menu map sound setup. Visible intro rendering does not establish a working
main menu or gameplay.
