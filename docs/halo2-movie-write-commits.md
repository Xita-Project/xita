# Halo 2 movie buffer write commits

The native98 movie path reaches a whole-buffer Lock after the explicit
unsupported-multibin diagnostic. This increment implements the audited paired
Lock/Unlock write boundary for the real PCM buffer. It returns the original
caller-owned addresses and copies completed writes into the actual mixer's
private sample mirror. Playback, cursor APIs, streams and surround processing
remain unsupported.

## Original contract and bounded adapter

Public Lock `37B7B3` is 48 bytes, SHA-256
`fc44b79ccde4fce54139e0db9a2d3b34c0af666e38d9363c0f935d480d1bde1b`.
It takes eight arguments and `RET 32`: buffer interface, offset, byte count,
first pointer output, first length output, second pointer output, second length
output, flags. It adjusts the public interface by `-1C` and invokes `37A9CF`.
That complete internal function is 226 bytes, SHA-256
`626162fe3a09ae5c427845fc6549e5f0f684dbfe00e59de19eb9dd99cb18fa42`.

With flags 0, the original first pointer is source+offset and first length is
the lesser of the requested length and bytes remaining to the ring end. A
wrapped request returns the source base and remaining length as the second
region; otherwise the second pointer and length are both zero. The adapter
matches these outputs for the supported whole-frame ranges. A private execution
check runs the actual public/internal functions on 36 offset/length cases and
verifies the returned regions and stack cleanup, with only the critical-section
helper isolated. Evidence is `../private/audio-host/movie-lock-original-check.json`
and `check_original_movie_lock.py`.

Original Unlock `379F40` is five bytes, SHA-256
`e123f60e9fc6e974d1381f2f15fb19e7960628cc8925d65e344c2f2bdc64f424`.
It takes the buffer, first pointer/length and second pointer/length, returns
success with `RET 20`, and performs no memory copy on Xbox. The audited Bink
helpers bracket their writes with this API. In this host adapter, Unlock is
the commit point: it copies the two completed regions into the unexposed PCM
mirror while holding the real mixer mutex once. The worker never reads the
source being modified by the game. This does not implement unrestricted Xbox
buffer coherence or arbitrary unpaired direct writes.

The supported subset requires a live, bound PCM buffer, flags 0, a nonzero
whole-frame count and a four-byte-aligned offset within one ring traversal, and all four output
variables mapped. Offset zero is valid. Only one outstanding Lock per buffer
is supported. Outputs must not alias one another, the argument/return stack,
adapter-owned pages or source pages. The source-page restriction is deliberately
conservative. Every output is validated before any output or lock state changes.

Unlock must exactly match the outstanding pointer/length tuple and retain valid
source/mirror mappings. Unmatched or duplicate commits stop before copying.
The final buffer Release is rejected while a write remains outstanding, so
mirror and parent resources cannot be freed before that commit. All existing
source ownership, real voice cleanup and parent device reference ordering are
retained. Existing SetBufferData remains a first-bind-only operation.

Both entries are fingerprinted only for the opt-in Halo 2 audio host. The
multibin diagnostic continues to return a genuine unsupported HRESULT and
preserve default stereo routing. No six-bin success, playback success or fake
cursor advancement is supplied by this increment.

## Validation

All 31 host executables plus timed/active channel modes and 42 Python tests
pass. The updated buffer/multibin failure executable passes ASan/UBSan. Tests
cover cross-page output variables, split and unsplit regions at ring boundaries,
full CPU/FPSCR return state, duplicate locks/unlocks, modified commit tuples,
unmapped data, physical aliases, output/stack aliases, invalid flags/ranges,
worker failure and rejection of final Release during a lock.

The actual shared-mixer test verifies that source changes remain absent from
the mirror until Unlock, that only committed bytes change, and that the source
itself is untouched by the copy. It then restores synthetic PCM through the
paired API and checks real stereo samples, volume/headroom effects, continued
parent ownership and silence after buffer release. Test-only direct mixer Play
remains separate from the unsupported guest playback API.

Diagnostics log only the first eight Lock/Unlock pairs per buffer, plus totals
at a terminal stop. They do not write a sample capture on each commit. Owned
image/code, disassembly, generated C, audio samples, packages and native traces
remain private and outside Git. Diagnostic VPKs embed owned game code/image and
must not be uploaded as distributable releases.

## Native99 result and replay

The original Bink clear calls Unlock from `3E3309` and commits all 106,496
bytes. It then creates its worker and calls the control helper again. The next
strict stop is SetMixBins `37C5E4`, return `3E321F`, ESP `005E5D58`: this time
the list at `005E5D74` contains two pairs, `(bin 0, volume 0)` and
`(bin 1, volume 0)`, at `005E5D7C`. The six-bin-only diagnostic rejects that
new request. The buffer has no outstanding lock, one commit and 106,496
committed bytes at the stop. This identifies a bounded stereo-route follow-up.

Four real output grains remain silent (`nonzero=0`, `peak=0`, no output error).
The only presented framebuffer remains black, with no original movie/menu
pixels. Vita3K's shader-compilation overlay is emulator UI. The decoded channel
snapshot is complete, with GET/PUT `03B43280`; no second-device draw is submitted.
Vita3K again faults at host address `2008C8` during `Running -> Stopping`, after
audio shutdown and all terminal snapshots complete. That emulator shutdown
fault remains distinct from the checked guest stop.

Private immutable evidence is `../private/native-99-artifacts/`,
`native-99-view/` and `native-milestone-99.json`. Build hashes (SHA-256):

| Artifact | SHA-256 |
|---|---|
| ELF | `023d1ebabf759b98b10391ebc9eb840655e6d3b47501fc7a3363210797d490ab` |
| EBOOT | `305935b67b2e45aea1fe10cbb2834988e8414073df9116f50b3a2a1a08982c32` |
| VPK | `f2a05abc04fce12a9200f5d4b3cdb4f28295439b02d7a3874e5e0049508800d8` |
| Boot trace | `2c7e0c66bbdec057db1adc6d3feaf81f8338dc7343788eb7897c1d80eeb31a51` |
| Channel JSON | `a2b3a19912c99b5a3e79150d46afd479d4b945b4679f2a369fa6ec0f4655bf00` |
| Black scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

The build uses `HOST_CHANNEL=1 QUAD_RENDER=1 AUDIO_HOST=1
AUDIO_EFFECTS_UNAVAILABLE=1 AUDIO_MULTIBIN_UNAVAILABLE=1 GUEST_OPT=-O0`,
from private `audio-movie-commits/`. Replay only in the isolated Halo 2 lab:

```sh
cd /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private
python3 run_lab.py replay99 native-99-artifacts/halo2-boot.vpk
# After the terminal trace and snapshots complete:
python3 run_lab.py stop
```
