# Audio completion during parallel object cache waits

A recovered physical gameplay log ends with an intentional object-worker STOP:
`DirectSoundDoWork` at `193E27`, return `29427`, in worker lane 1. The indirect
call chain is `C3A00 -> 29420`; its callers include the bitmap-cache wait at
`32685`. The private guest stack canary is intact, with roughly 95 KiB of the
256 KiB allocation observed in the preceding stack reports. This identifies
this run's rejected service call; it does not explain every earlier GPU crash.

The same log confirms actual work on both object lanes, with recent utilization
samples near C0 40%, C1 38%, C2 31%. Higher utilization alone does not establish
a frame-rate improvement.

## Change

The existing owner-service queue now admits this exact audio-pump caller. Both
object lanes park before the owner invokes the existing DirectSound handler.
Packet completion status, completed byte count, and the original game callback
execute normally on the requesting job's private stack. The job marker stays
intact; the request does not impersonate a different guest fiber or enable
arbitrary scheduler calls.

Halo's stream completion callback `29590` can refill through `28B00` and
`28870`. Their GetStatus and Process vtable calls are admitted only during the
active owner audio service, on the owner thread, with their exact return sites
(`28B35` and `289FD`). They execute inline, avoiding a recursive request that
would wait for its own servicing thread. Other nested owner services stop
explicitly before entering that queue. Unknown callbacks and error paths retain
the experimental worker's existing strict checks.

The shared indirect-call cache is not accessed by these worker-context calls.
Only the two immutable audio vtable entries gain this guarded path. No shader,
game function, audio mixing algorithm, or graphics setting changes in the patch.
Ordinary builds omit the object-worker code as before.

## Validation

`tools/test_object_jobs.py` compiles the production worker pool, indirect
dispatcher, DirectSound packet pump, GetStatus and Process bodies. Deterministic
audio-device and guest-callback fixtures check completion followed by refill,
pending packets remaining pending, the owner thread, original stack cleanup,
and publication of every parked lane's writes. It exercises 600 audio pumps
per run with two workers, one worker and owner-only execution, both inside and
outside the shared transaction lock. ASan/UBSan and ThreadSanitizer pass.

Invalid top-level audio callers, direct worker stream calls, and recursive owner
RPCs are rejected. Existing events, cache I/O, resource queries/registration,
vertex-lock tests, ordinary indirect dispatch and audio mixer tests also pass.
These fixtures do not prove the independence of Halo's entire object graph.

The native package builds and retains the installed updater contract: only
`game-a.self` and `boot-game.txt` differ. Runtime SHA-256:
`1164d03e362f87618da9740bf8ff9cb677c0b603afac1dac1af7a4a08a4fdd54`.

In the private emulator, the normal menu loads Blood Gulch; two charged plasma
shots, turning and movement complete with both object workers active and no
STOP in the captured log. All observed owner-audio-pump counters remain zero:
this is an integration check, not a reproduction of the physical cache wait.
Physical execution of the repaired path and representative driving/campaign
stability remain required. No FPS improvement is claimed for this fix.
