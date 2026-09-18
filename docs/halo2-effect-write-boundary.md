# Halo 2: original menu effect-data boundary

Native144 reaches the original menu sound setup after successful map loading
and movie cleanup. It still stops at `37B60D`, caller `191294`. The new bounded
terminal probe reads the checked guest arguments, copies eight mapped source
bytes, and reads the real GP state under the existing audio worker mutex. It
never changes data, CPU state, results or control flow. Published descriptor
metadata is read from the mapped guest snapshot, without accessing mutable
interpreter state outside its lock. All 44 host executables pass and all 170
native dependency targets were validated.

The observed call is device `00936008`, effect 4, offset 32, source `007342BC`,
eight bytes and flags 0. The supplied words are `00000000,0000001E`, matching
the current GP words at that offset. The published state view is `00947434`,
288 bytes; the code view is `0094D900`, 312 bytes. No successful effect-data
setter is supplied by this diagnostic.

## Audited original behavior

Original `191270..1912F1` calls the same public setter for effects 4, 5, 6, 7,
all offset 32/eight bytes/flags 0, with return addresses `191294`, `1912AC`,
`1912C3`, `1912DB`. The public wrapper adjusts the device interface by eight,
calls `37A2ED` and returns with 24 argument bytes removed. That method passes
the operation to `37E52F` after the original synchronization check.

Four private original-code oracles execute 120 distinct instruction addresses
each through the complete immediate setter. Descriptor lookup rejects an
out-of-range index with `88780032`. On a valid index, `383D79` copies the
input bytes to the image shadow. With flags 0, `37E5C6` then copies the same
words to the effect's GP X-memory pointer. Flags bit 0 instead records a deferred
range and does not take that immediate copy path; it remains outside the
proposed first adapter. The fixtures supply an owned-image-derived descriptor
layout and mapped guest/GP/shadow memory. They do not emulate physical APU
registers, DSP execution, or asynchronous hardware timing.

The loaded image places the four effect states at offsets `41CC,42EC,440C,452C`,
each 288 bytes. Their offset 32 pairs map to GP X-word addresses
`115,15D,1A5,1ED`. All four image-shadow pairs initially contain 0, 30. The
interpreter's live read confirmed that pair for effect 4 at the actual stop.
This is parameter-write evidence; it does not establish that arbitrary effect
writes, unaligned bytes, deferred commits or high-bit word values are supported.

The next implementation should validate the exact public ABI and original
callers, preserve both image-shadow and live GP state under the mixer lock,
and retain all other DSP/filter history and pending sink ownership. Tests need
changed and unchanged parameter pairs, rejected ranges/flags/aliases without
mutation, real subsequent DSP consumption and native replay. A read match at
this one stop is not a general sound-success substitute.

## Private native evidence

| Native144 artifact | SHA-256 |
|---|---|
| ELF | `4607e8ad7a75e4ca369587423fa26ef88d19ac16e588c8fbb2d35e7eb4850dba` |
| EBOOT | `c7e2e31d5b3238b619886463b6731cd21648cda2602a53312f7ecf59a8168895` |
| VPK | `27622012988585af487878cc164df0e89b2f40c74d494944664eaf51a8e932a3` |
| Guest trace | `3683730683557731a2699e256537b77c04ef3171016c8388552d0c22ceb74a68` |
| Final black frame168 | `7628ec62e67708a9afb3462e5eb5af33eb0dcd9ea9ff85050bbc30e86940c699` |

Evidence is in `native-144-artifacts`, `native-144-view`,
`audio-effect-write-probe/native-build-identity.json`,
`audio-host/effect-write-original.*`, and `dsp-bringup/effect-write-probe-*`.
The game observes normal Start after the focused eight-second press at
22:20:51.430 UTC. The owned process has stopped and the worker closes cleanly.
Rendering is investigated separately; native144 still displays black and no
main menu. Build uses `audio-effect-write-probe/build` and unchanged generated
source/image. Packages embed owned game content and must not be uploaded or
distributed; all game data, generated code, traces and snapshots stay private.

Native145 uses exactly the same ELF/EBOOT/VPK and reaches the same strict
setter with the same arguments and values. A controlled private SPIR-V replay
restores the visible original intro; see [the rendering control](halo2-spirv-intro-replay.md).
Normal Start was observed as digital `0010` after the eight-second press at
22:27:45.888 UTC. The worker reports 5,332 grains, 328 nonzero grains, peak
4,266, error 0, and terminal close result 0. Audio is routed to the SDL dummy
sink in this lab; these counters are not evidence of audible speaker output.
The final black frame is frame 286, and no main menu is displayed.

Native145 trace SHA-256 is
`84eecc585c857b6feb5c03667cecb223f430926910763fa0838200b97711d8fc`;
its completed channel snapshot is
`cea933d1bbe504459313c5dc19c956188dcfc5879d1415d28c6f88e2b3236b8b`.
The complete private artifact manifest is `native-milestone-145.json`.
