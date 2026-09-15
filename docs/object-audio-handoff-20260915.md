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

## Physical follow-up: rocket launcher pickup

The audio candidate was installed through Wi-Fi with its exact hash confirmed
in slot B; the previously working slot A was preserved. Standard texture detail
and original material, particle, glow, decal and model settings were verified
at 360p, with the frame cap off. The user observed work on all three CPU cores
without a noticeable performance improvement.

The next pickup-related crash has a different explicit STOP: yield `1D6640`
via `12AA9`, caller `17A804`, inside `17A750 -> 17A840`, indirect object callback
`44AD0`. The stack canary remains intact. No audio-pump service was reported in
the captured completed windows, so this run does not validate that repaired
path or establish that it caused this failure.

`17A750` preloads a sound tag's cache entries. `32A70` submits missing data
through `33A20`, passing the entry's ready byte at offset 2 as the completion
pointer. The preload's yield is reached when that same ready byte is zero.
`33A20` uses the queue and event consumed by the already supported `33AF0` file
fiber. The follow-up admits only this additional return site to the existing
quiescent file-fiber handoff. It does not allow general yielding from workers.

The follow-up worker tests cover 600 cache yields per run, including 200 from
the preload site, with two, one and zero worker threads. Plain, ASan/UBSan and
ThreadSanitizer runs pass. The production file-fiber test also passes 100
event/read-completion/APC cycles with preserved owner/fiber state and rejects
unrelated file-thread targets. The native package retains the updater contract.
Its runtime SHA-256 is
`c90d8400839cfd62c2a3b98b7d6c1c961136c41b10a844609fb8f7f332623f8a`.
The exact candidate reaches Blood Gulch through the normal menu in Vita3K;
the captured integration log contains no STOP. A physical rocket-pickup retest
is still required.

## Performance lead from the failed run

One 60-pass object report records 1,138,967 us of joined batch time, with
497,106 / 547,906 us of elapsed mutex waits on the two lanes: approximately
19.0 ms per pass, including 8.3 / 9.1 ms of waiting per lane. These times overlap;
they are not additive CPU costs. This window reports no cache I/O services.

The render reports contain multiple simulation passes per displayed frame.
Their independent reporting windows do not align exactly, so dividing the
19 ms pass cost by one rendered frame would understate its contribution, and
adding whole independently collected windows would overstate precision. The
next useful measurement is contention by shared transaction/helper within the
same rendered-frame interval. The run's gameplay render windows span roughly
5.2–7.1 FPS with changing views and initial texture loading; they are not a
controlled performance comparison.
