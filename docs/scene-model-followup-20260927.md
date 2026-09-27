# Scene/model attribution follow-up — September 27

With the profiling-stack exclusion in place, a bounded 120-second Pi campaign
capture completed (planned timeout 124). Object workers were explicitly disabled,
matching their observed admission state on perf272 hardware. Both scene and tick
phase observers were enabled. Private receipts: ../scene-followup-pi/.

Last ten reported windows' median inclusive / remainder milliseconds:

| Scope | Inclusive | Remainder |
|---|---:|---:|
| 5DBC0 scene root | 24.575 | 0.010 |
| 5B760 model-list pass | 11.825 | 0.060 |
| A26B0 model packet | 9.560 | 8.160 |
| 54010 ordered callbacks | 2.200 | 2.200 |

Inclusive rows overlap. Remainder is NOT pure arithmetic or removable execution:
untimed callees and native waits remain charged to parents. The headless harness
also differs from the current Vita runtime, so these are targeting evidence,
not FPS predictions or a controlled hardware comparison.

Inspection of the retained A26B0 body found two A2380 calls without child timers:
UV-scope setup separates each call from its guest return push, so the older
push/call-only patcher missed them. A2380 calls 70110, 6B060, 6EFC0 and B5EA0.
This explains an attribution gap; it does not establish their relative costs.

A private follow-up shard adds existing --any-call observers to A26B0 and A2380
(14 call sites total, no patcher warnings). All other linked objects are retained
from the corrected harness. The ARM build passed and its bounded capture is
running under ../model-material-split-pi/. Inspect its result before deciding on
a native rewrite; do not treat the 8.16 ms remainder as A26B0's own cost.

Related regression checks also passed: phase off/both/tick-only behavior,
stack-overflow recovery, and hash-hint lookup equivalence after table reordering.
