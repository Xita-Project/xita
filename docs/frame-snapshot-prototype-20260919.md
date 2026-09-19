# Two-buffer snapshot ownership prototype

`xk_frame_snapshot.c` provides a bounded single-producer/single-consumer exchange
for caller-owned byte buffers. A consumer pins an immutable generation. The
producer can fill and publish the other buffer, but cannot reuse a pinned buffer.
Acquisition uses at most two attempts; beginning a write returns NULL when busy.
There are no semaphore waits, heap allocations, or generation wraparound.

The pthread fixture passes 50,000 publications while checking entire payloads
across deliberate yields with ASan/UBSan. It also covers initial empty state,
overlapping storage rejection, oversize publication, cancellation, duplicate
acquisition, pinned-slot backpressure, and generation exhaustion. VitaSDK ARM
compilation passes with a compile-time lock-free unsigned-atomic requirement.
These results do not establish ARM hardware concurrency or engine integration.

This is **not enabled or connected to gameplay**. No existing joins are removed.
It does not snapshot the world, redirect guest memory reads, or establish task
independence. Snapshot payloads must eventually include every render-visible
mutable dependency and object lifetime information; guest pointers into live
mutable storage are insufficient. CPU and any GPU readers must retire before
release. Both buffers and the exchange must outlive all users; initialization
or reset during concurrent use is unsupported.

The immediate priority remains the crowded 5B760 per-model loop. This ownership
component is preserved for subsequent input/result snapshot integration, not
reported as a performance optimization or a completed simulation/render split.
