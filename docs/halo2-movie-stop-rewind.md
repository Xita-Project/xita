# Halo 2 movie Stop, status and rewind

This milestone implements the original movie buffer's Stop/GetStatus/zero-rewind
sequence using the actual mixer and output ownership. Native105 passes those
original calls and observes a normal Start press leading into movie cleanup.
Its 200 presented frames remain black, with no visible movie or main menu.

## Original boundaries

The native104 caller `3E3850` calls public Stop `37B703` at `3E389C`, then polls
GetStatus `37B75B` until its output is zero, calls SetCurrentPosition `37B797`
with zero, and clears the original Bink ring through its paired write path.
Only these bounded public behaviors are supplied. Hooks require `AUDIO_HOST=1`
and exact owned-XBE fingerprints:

| Entry | Guest ABI | Bytes | SHA-256 |
|---|---|---:|---|
| `37B703` | buffer / RET4 | 24 | `98c7d0ea4384e7c62b0da83d228fc5451f7490839016b9c0a0aee8dfdec6d785` |
| `37B75B` | buffer, output / RET8 | 28 | `680f71631d4b31fb3c63170a4718a9da26670c5ae6e032ffcd88e0dd949c3e35` |
| `37B797` | buffer, position / RET8 | 28 | `a62c7816ddd7eca894ee66b02240910e0bcc9fa1103626071bf1c13e3bc63e76` |

Original Stop wrapper `37A830` calls `382DD2` with flags zero. For the observed
`A0` buffer, this saves its current play cursor, stops the active hardware voice,
and processes any notification list. The supported buffer description has no
notifications. Repeating Stop when already stopped returns zero without another
hardware stop. Original status helper `3828D6` reports 5 for the ordinary active
looping flags `203`, and zero for the completed stopped state. Other original
status bits exist; this adapter does not pretend to support those modes.
Original position helper `382E62` stores the supplied position directly when
stopped. Only the observed zero rewind is supported here.

The private original-code oracle executes the public wrappers and original
state logic, checks the saved position, idempotent Stop, six status-bit cases,
stopped zero seek and subsequent zero cursor outputs. Critical sections and
hardware position/stop are isolated with controlled fixtures, not emulated APU
success. Owned bytes and the oracle remain outside Git in
`../private/audio-host/check_original_movie_stop.py` and
`movie-stop-original-check.json`.

## Real output and lifetime

Stop disables the real mixer voice, prevents a new grain submission, and waits
for the already submitted grain's actual remaining count to reach zero. It
then preserves the consumed source position as both stopped cursors. There is
no cancellable sink API in this adapter: completing a retained grain adds up
to one 1,024-frame output grain of normal latency, plus host scheduling. It does
not reproduce Xbox immediate hardware-stop timing. Invalid/stalled sink
observations are strict failures and do not report a completed Stop.

Status is checked against the real mixer's playing state and its output progress
record. It reports playing/looping or a completed stop, with validated mapped
output storage. A stopped rewind sets the actual mixer position to zero and
invalidates its decoded block. The original looping Play can then restart from
that explicitly rewound position; arbitrary positions, running seeks and a
restart without rewind remain strict unsupported cases.

A stopped buffer can release its final reference only after the progress record
releases ownership. The mixer voice is freed before its sample mirror and guest
object, retaining the existing parent-device lifetime. Silent output queued
between Stop and Release remains owned by the worker; its samples are never
credited to a later voice. Repeated Stop preserves an already rewound position.
Streams, surround routes, effects and other play modes remain unsupported.
No shared Halo CE source or audio behavior changes.

## Validation

All 32 host executables, additional channel modes and 42 focused Python checks
pass. The actual shared mixer/guest ABI test verifies three Stop/rewind/restart
cycles, silence after Stop, matching stopped cursors, zero cursor after rewind,
nonzero PCM after restart, rejection before mutation, final release and safe
subsequent object reuse. The concurrent worker test verifies stopped cursor
stability while quiet grains continue, real remaining-sample observations,
failed Stop/drain retention, restart and progress ownership release. Pure-state
tests reject Stop while an active grain remains, retain queued silence through
rewind/restart/forget and preserve exact sample accounting. All three suites
also pass ASan/UBSan.

## Native105 and normal Start

The native VitaSDK build passes. In the isolated `:111` replay, the original
caller completes 43 Stops, 41 zero rewinds and 42 looping Play calls. The two
final Stops come from Bink cleanup returns `3E3906` and `3E39B2`. The adapter
retains the original loop and return values; it does not force movie completion.
The real sink accepts 2,857 grains, 455 nonzero, peak magnitude 1,327 and no
backend error. The lab remains muted with SDL dummy output: no physical speaker
audibility is claimed.

Enter/Start is sent at epoch `1789397442.2942033` and released at
`1789397444.2948895`. The original input query records `digital=0010`, followed
immediately by Bink cleanup. Its original worker exits, the audio buffer's final
reference releases its real voice/mirror, and the remaining device has one
reference and no buffer. The next strict stop is public device Release `379F2A`
from return `3E39FD`, ESP `005E5E10`, interface `00B76008`. That caller is not yet
allowed by the original-method guard. Terminal worker/port close returns zero
and all snapshots complete. The owned emulator is stopped after archiving.

The last-presented snapshot has frame counter 200 and all 522,240 pixels are
`FF000000`. The live window capture is also black. Independently decoding the
owned intro with local FFmpeg shows frames 0..15 have uniform black luma 16;
frame16 begins a fade. That explains why native104's few black submissions did
not alone establish a fault, but does not explain or validate native105's later
black output. Decoder frame position, texture content and the rendering boundary
still need evidence. No original movie/menu visibility is claimed.

This replay also logs the known populated-cache4 raw-access limitation before
its map-state transitions through state8. The next replay must preserve that
private cache directory aside and use an empty private cache, as described in
the earlier [cache diagnostic](halo2-exit-cache-diagnostics.md). This does not
modify the owned maps or provide a general FATX driver.

| Native105 artifact | SHA-256 |
|---|---|
| ELF | `0be868f9f69acf6bd23cf98075c630f5e60fa11840afb9f9c5d3a1273dd49a40` |
| EBOOT | `bcdd8892b4a94a3850ce35678a1db353034c088732ff94ca2751bfa9d6f73fb2` |
| VPK | `803fcbb7e05bf741181ce15699c5275728cf7e1b70282b65c67531d4ed2490b8` |
| Boot trace | `41dec19fd4dd121ed2e6d082018c94eb5de28932dd25e2af16a4473c124a3adc` |
| Channel JSON | `aed97785e9749a7269eb2f04dea65c83eeedffabac9939fd81a459d7f0188889` |
| Last presented | `99db702ce3b48094db2176de4ac7b056b36a3d66f5e3a0f61a762143567e99ae` |

Frozen artifacts are in `../private/native-105-artifacts`, generated source and
build in `audio-movie-stop`, captures in `native-105-view`, and the actual key
injection timestamps in `movie-skip-audit/native105-late-input.json`.
From `../private`, replay `python3 run_lab.py replay105
native-105-artifacts/halo2-boot.vpk`, then send normal Start once the original
map-loading gate permits movie input. Stop only this lab with
`python3 run_lab.py stop`. The diagnostic package embeds owned game code/image
and must not be uploaded as a distributable release.

Next, audit the `3E39FD` Release caller and finish the original movie cleanup,
then trace the resulting map startup. The separate
[effects-unavailable initializer limitation](halo2-effects-image-audit.md)
remains unresolved; no state or object is fabricated to bypass it.
