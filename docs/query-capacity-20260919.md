# Larger collision-query records

The [perf.21 retention trial](query-retention-20260919.md) preserved completed
records but abandoned 47 of 56 capture attempts with capacity overflow. Perf.22
raises the fixed memory-record limit from 128 to 512 blocks of 64 bytes. The
mapping limit remains 128; new occupancy counters distinguish the two limits.

The block hash grows from 256 to 1,024 slots and uses the corresponding high
product bits. Compile-time assertions tie its shift, capacity and 16-bit record
indices together. Capture still initializes blocks lazily, and validation still
checks all recorded dependencies before publishing any write.

Host memory-record size is 80,944 bytes. The 64-history/16-record adapter pool
grows from 354,816 to 1,313,280 bytes (958,464 additional bytes). The adapter
remains behind the default-off `XV_QUERY_REUSE` option; ordinary builds do not
allocate this pool.

`[query-reuse-detail]` now reports maximum/total blocks and mappings across
captures and both capacities. Occupancy includes failed captures, so reaching
a limit is visible. These counters are updated under existing ownership and
reported after workers join; there are no per-access clocks.

Host and ASan/UBSan memory/capture/adapter tests pass. The full-capacity test
checks read dependencies and replay writes in the enlarged table, including
atomic rejection for a changed dependency in its final block. Actual ARM query
comparisons and the 192-unrelated-query retention test also pass. Private
qualification evidence is under `../collision-query-reuse-capacity/qualification/`.

## Hardware result

The native perf.22 package is built from `d9cd33d+` with prior cumulative options
retained. The remote updater verified runtime SHA256
`20010e42c2d00dd84a5ba3011224a0213ca65a0f7f9f44c56155e241750470dd`
and confirmed boot in slot 0. Slot 1 retains perf.21.

The same Normal Pillar of Autumn checkpoint loaded at camera
`(-28.66, 32.52, 0.62)`, forward `(0.56, 0.82, -0.15)`. Twelve settled windows
after a quiet gameplay interval averaged **78.075 ms / 12.81 FPS / 151.5 draws**,
essentially unchanged from perf.21's 78.15 ms / 12.80 FPS / 152.5 draws.

Across 16 reported windows including transition:

* 11,100 calls, 5,902 coarse repeats and 48 completed captures.
* No abandoned captures or CPU/memory validation rejections.
* 1,971 successful replays and 1,782,677 saved execution-budget units.
* At most 372 blocks and 107 mappings in a captured query; both limits suffice
  for this observation. The 16 transaction slots fill and then replace older
  records as further candidates qualify.

No searched fatal/stop/GPU-fault/data-abort markers appeared in the captured log.
This is a short checkpoint observation, not sustained moving-gameplay proof.

The object-active scope averaged 26.23 ms/frame versus 27.16 ms on perf.21;
the scene-owner scope averaged 40.85 ms versus 40.03 ms. These are inclusive
elapsed scopes with scheduling/waits and live-scene variation, not CPU self-time.
They do not prove the cache caused either difference. Whole-frame performance
remains unchanged despite substantial query reuse. Saved budget units are not
saved wall time.

This resolves the capture-capacity question; it does not justify further blind
capacity increases. Next separate original/capture/replay costs and account for
remaining scene work before choosing further cache tuning versus a native hot
routine or independent-job change. The experimental option remains default-off.
Private receipts, captures, log, summary and scope comparison are in
`../ce-perf22/`.

The follow-up [elapsed attribution](query-cost-attribution-20260919.md) measures
the full selected adapter at 3.89 ms/frame; further capacity tuning is no longer
the priority for closing the campaign frame-time gap.
