# DirectSound playback reporting

September 8: the [menu-audio follow-up](weapon-menu-20260908.md#crackly-menu-music)
adds page-aware sample reads and two aligned output buffers. The reporting model
below is unchanged; audible hardware verification remains pending.

The HLE is in `recomp/kernel/xd3d.c`; the reporting clock arithmetic is in
`recomp/kernel/dsound_state.h`. `xk_audio.c` decodes/mixes and `xk_os_vita.c`
submits the resulting grains through `sceAudioOutOutput`. Those output paths,
voice-control calls, packet completion callbacks and their pacing are unchanged.
The formerly inert voice-stop helper changes reporting state only.

## Model

Each tracked buffer/stream has game-thread-owned reporting state independent of
its mixer voice: byte length, format tag, channels, bits, block alignment, native
sample rate, frequency override, monotonic start/deadline, duration, saved offset,
looping, stopped and paused flags. Creation starts stopped. There are no new
allocations, mixer locks, sleeps or callback dispatches in the query/stop helpers.
The fixed tables retain their existing limits (256 buffers, 128 streams); reporting
adds approximately 38 KiB including per-packet deadlines.

`xk_os_monotonic_us()` uses `sceKernelGetProcessTimeWide()` on Vita. PCM duration is
`ceil(bytes * 1000000 / (channels * bytes_per_sample * effective_rate))`.
Xbox ADPCM (tag `0x69`) uses 64 decoded frames per channel block, hence
`ceil(bytes * 64 * 1000000 / (block_align * effective_rate))`. This avoids treating
4-bit ADPCM as PCM or rejecting low mono byte rates. Format defaults/validation
match the existing mixer. An unspecified format is 48 kHz stereo PCM16.

Play sets the deadline to start plus the remaining duration. Stop saves the
modeled offset; replay resumes it, or restarts at zero after natural completion.
SetCurrentPosition and SetBufferData update the modeled offset/length; format and
frequency changes preserve elapsed source position and retime the remainder.
A non-looping buffer reports playing precisely while `now < end_us`, unless
stopped or paused. A looping buffer stays playing until stopped (or paused).
There is no separately recognized Pause entry point in the current 3925 HLE:
the paused flag is reserved and checked, while the existing Stop/Play surface
provides stop/resume. SetPitch remains the pre-existing no-op, including output.

Each accepted stream packet appends its duration after `max(now, previous_end)`.
Its separate reporting deadline tracks the modeled queue without changing the
existing completion deadline. Playback remains true while modeled packets are
queued or the final packet's deadline has not passed, even if the mixer already
removed that packet. Conversely, callback-pending packets whose modeled deadlines
have passed cannot keep playback true forever: Halo busy-polls this helper in
some paths without calling DoWork. Queries neither pump nor complete packets.
Stop immediately suppresses reporting and clears modeled packet deadlines;
Flush also stops reporting; the next accepted packet starts playback again.
Stream frequency changes retime all remaining reporting deadlines.

## Exact 3925 return contract

Disassembly of the user's `default.xbe` at `0x0019C5E7` shows:

```asm
mov eax,[esp+4]
mov eax,[eax+24h]
mov eax,[eax+8]
and eax,10000002h
neg eax
sbb eax,eax
neg eax
ret 4
```

This is a normalized **BOOL in EAX (0 or 1)**, with one callee-cleaned stack
argument and no status out-pointer. It tests internal voice flags, not a public
DSBSTATUS DWORD. At `0x28522`, Halo calls it, copies EAX to EBX, then executes
`neg ebx; sbb bl,bl; inc bl` to compute "done". Calls at `0x287EC` and `0x2918B`
similarly invert the result; the latter branches back to poll while playing.

The HLE resolves a registered object directly (creation replaces the entire
DirectSound interface), or a wrapper's `+0x24` object pointer. Null/unregistered
objects return zero. `DSoundVoiceIsPlaying` returns exactly EAX=1 while playing,
otherwise EAX=0, and `X_RET(1)` preserves the `ret 4` stack contract.
`0x19C5FF` originally dispatches a method through the inner object's vtable at
`+0x10`, passing two zero arguments, then does `ret 4`. Its replacement marks
reporting stopped immediately, returns EAX=0, and uses `X_RET(1)`.
The `tools/recomp.sh` address overrides remain unchanged.

`IDirectSoundBuffer_GetStatus` is a different surface: it returns HRESULT S_OK
(EAX=0), writes `DSBSTATUS_PLAYING` (1), plus `DSBSTATUS_LOOPING` (4) when actively
looping, to the supplied DWORD; otherwise it writes zero. Stream XMO GetStatus
continues to return its original `ACCEPT_INPUT_DATA` bit, not playback status.

## Logging and verification

Set `XV_DSOUND_LOG=1` before launch (Vita `xita.cfg`, emulator `env.txt`). The flag
is read with getenv once, and is disabled by default. `xita.log` then contains
every tracked create/play/stop and the first 256 observed IsPlaying transitions
across all voices. Entries include object, bytes, format/rate, looping, duration,
start/deadline in microseconds, and queue count. Transitions are observed at
queries (including buffer GetStatus), so an unqueried short sound need not emit
a transition pair. Stream play entries describe the newly submitted packet.

Host regression check, using the actual query HLE with a fake monotonic clock:

```sh
cc -std=gnu11 -O1 -ffunction-sections -fdata-sections -Irecomp -Irecomp/kernel \
  tests/dsound_state_test.c -Wl,--gc-sections -lm -o /tmp/dsound_state_test
/tmp/dsound_state_test
```

It covers exact expiry, looping, pause suppression, stop/replay, rate changes,
mono/stereo ADPCM duration, empty buffers, direct/wrapped/null/unknown objects,
stream final drain/stale callback queues, and EAX/out-DWORD/stack contracts.

Hardware/emulator verification remains required: replay a scripted a10 run with
logging and correlate dialogue deadlines with cinematic progression, cryo tutorial
and sound-completion waits; repeat on a30. Check late packet submission, underruns,
stop/restart and looping ambience. Compare Blood Gulch effects/music with logging
disabled. The wall-clock model does not measure device latency or mixer starvation,
so audible completion can differ by queued output grains or a device stall.
Stream release now stops reporting; existing mixer resource/refcount handling
and table exhaustion are unchanged. Long
sessions should also check those limits. No hardware/emulator run is claimed here.
