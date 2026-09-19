# Keep completed world-query records across ordinary misses

The [perf.20 trial](query-selective-reuse-20260919.md) produced only three
replay hits in 15,294 calls. Its single 16-entry ring discarded completed
transactions alongside ordinary repeat history. Perf.21 separates those roles.

* 64 lightweight entries track candidate keys, observations and work estimates.
* 16 transaction slots retain complete CPU/memory records. Their history entries
  are pinned while the records remain valid.
* Ordinary misses replace unpinned history only. When all transaction slots are
  occupied, a new capture replaces the least recently used transaction and
  cools its previous history entry down.
* Dependency rejection releases a record immediately. Root changes, callback
  admission loss and epoch wrap reset ownership using the previous protocol.

Host storage grows from 349,312 to 354,816 bytes. Original queries, capture
instrumentation, replay dependencies and the existing actor guard are unchanged.
Capture still requires eight observations and at least 32 prior work units;
global capture throttling and failed-record cooldown remain in place.

`[query-reuse-detail]` adds retained-record counts, separate observation/work
eligibility counts, record replacement counts and CPU/memory rejection reason
bitmasks. Bit definitions live in `xk_query_cpu.h` and `xk_query_memory.h`.
The original summary's eviction count now describes lightweight history churn.

## Validation

Host and sanitizer checks cover completed-record pinning through 192 ordinary
misses, all 16 transaction slots, least-recently-used replacement, slot release
on dependency rejection, ownership consistency and overflow reason reporting.
The previous admission, callback, atomic fallback and epoch tests still pass.

The actual ARM adapter again matches original complete memory, context and FPSCR
for split, winding, edge and negative queries, including changed dependencies
and short-budget fallback. A separate ARM test inserts 192 unrelated queries
between capture and reuse, checks each against the original implementation, and
then successfully replays the retained transaction. It records 129 history
evictions and zero transaction evictions.

Private ARM evidence is under
`../collision-query-reuse-retention/qualification/`.

## Hardware result

Perf.21 (`875492a+`) was remotely hash-verified and boot-confirmed in slot 1;
slot 0 retains perf.20. Runtime SHA256:
`c4a6d9a6ff540712d605637b5ad5c99f21ead38c6474617ac54c7ceb574f7399`.
The same Normal Pillar of Autumn checkpoint loaded at camera
`(-28.66, 32.52, 0.62)`, forward `(0.56, 0.82, -0.15)`.

After a quiet 60-second gameplay interval, twelve settled reporting windows
averaged **78.15 ms / 12.80 FPS / 152.5 draws**, essentially unchanged from
perf.20's 78.25 ms / 12.78 FPS / 151 draws. This is not a demonstrated gain.

Across 16 windows including transition, 11,370 calls produced 5,883 coarse
repeats, 56 capture attempts, nine completed records and six successful replays.
Completed records grew to nine with zero transaction evictions. All 47 abandoned
captures reported memory reason mask `05`: capacity overflow followed by the
finish check rejecting the invalid record. CPU reason masks remained zero.
The log contained no searched fatal/stop/GPU-fault/data-abort markers; this is
a short-run check, not sustained gameplay stability proof.

The retention mechanism works, but recorder capacity now prevents most capture
attempts from completing. The next bounded trial should increase recorder
capacity and report actual block/mapping occupancy. Do not weaken dependency
validation or assume a larger recorder will yield useful hits: surviving reuse
and total frame time still determine whether this approach is worth keeping.
The feature remains default-off.

Private package receipts, load observations, screenshot, gameplay log and
summary are under `../ce-perf21/`.
