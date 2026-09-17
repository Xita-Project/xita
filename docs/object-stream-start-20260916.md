# Object-worker sound stream startup and refill

A retained physical campaign run of runtime `c8f54f0f…` ends at approximately
2,997 seconds with an explicit worker STOP in deferred audio commit `193C1B`,
returning to `28BB6`. The indirect chain is `C3A00 -> 2A210`; the private stack
canary is intact. This identifies a software safety stop in this run, not a GPU
driver fault. The disabled collision-solver prototype was not installed.

The complete log is preserved privately as `audio-refill-crash/crash-run.log`
under the September 16 direct-cluster-query validation directory. SHA-256:
`9d87d5c2266acb071fc190f167ea06b68d808a7602d6bf7105ec6a35015bb211`.

## Change

The audited startup routine `28B70` commits deferred settings before refilling a
previously idle stream. Its `28BB6` caller now uses the existing audio owner with
other workers parked, just like the earlier `291EF` update caller. The original
handler, return value and one-argument cleanup remain unchanged.

The successful startup immediately continues through `28B00` and `28870`. Their
stream GetStatus (`19384F`, return `28B35`) and Process (`193884`, return `289FD`)
calls now also use the parked-worker owner. Both existing handlers can complete
packets and invoke the original completion callback. Nested calls from that
callback execute inline on the same owner, avoiding a recursive owner request
that would deadlock. Other callers and unsupported nested services still stop.

Two additional joined-report counters record startup commits separately from
ordinary deferred commits, and owner status/packet requests separately from audio
pumps. A positive startup-commit count specifically identifies the newly admitted
`28BB6` path. Existing worker configuration and graphics settings are preserved.

## Validation

The production worker suite passes with two, one and zero workers, wait profiling
and bounded waits each off/on, under ordinary compilation, ASan/UBSan and
ThreadSanitizer. Each configuration exercises 600 commits (300 startup), 600
status requests and 600 packet submissions, both inside and outside held shared
transactions. Tests cover known/unknown streams, real HLE packet completion with
nested refill, return values, stack cleanup, owner execution, parked-worker
publication and counter reset. Unrelated callers and recursive owner requests
remain rejected.

The Vita build passes. Package verification requires exactly the same 1,588
members and updater asset contract as the installed build; only the runtime and
boot marker may differ. The only changed object is the worker bridge, and the
collision-solver experiment remains compiled out.

The physical updater verifies runtime
`847524f8e69175ba0db88c1c89fd71afe19819a4d2536200392b1946ca528e09`
in slot A0, with a confirmed dashboard boot and no pending update. Previous
runtime `c8f54f0f…` remains in slot B1. Execution of the newly admitted startup
path is still pending. This is a targeted stability fix; no FPS improvement or
general combat stability is claimed.
