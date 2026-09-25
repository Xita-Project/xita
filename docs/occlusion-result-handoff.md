# Occlusion-result ownership

The completion pump previously updated `xk_occlusion.c`'s object table and result
counters while the scene helper read/updated the same ordinary fields. A GPU
fence protects the GPU result buffer, not this CPU table. This audit establishes
shared unsynchronized accesses, not the cause of any particular crash.

The candidate sends five-word completion events through a single-producer,
single-consumer queue. The pump owns publication; the serialized model-render
path consumes events and alone updates the object table. Existing frame reports
run after the scene join, so they no longer race pump updates to result counters.
No queue lock, semaphore, spin wait or GPU lifetime change is introduced.

The 1,024-entry queue uses aligned 32-bit release/acquire counters, permits
unsigned sequence wrap, and never overwrites unread payloads. A full queue drops
the new event and sets a sticky flag. Once observed, the consumer disables both
occlusion skip policies until restart, rendering objects normally. Each consume
call handles at most 1,024 events. A producer crash or missing result cannot
force the render thread to wait. Queue memory is about 21 KiB.

Host ASan/UBSan and Pi ARM tests cover full/empty, sequence wrap, sticky loss and
one million ordered concurrent events with all five payload fields checked.
The modified occlusion unit compiles with the retained Vita flags. These checks
do not replace integration testing of table collisions, skipped/rendered object
transitions, reports and GPU completion. The integrated render-admission fixture also passes on host and Pi: hidden/visible
transitions, missing proxy, own-sample precedence, table collision, stale results,
frame wrap, and overflow fail-open. The full perf222 Vita build passes. Physical perf222 testing confirms active culling with no reported result-queue
overflow in the settled a30 lifepod capture. Across 1,200 Present intervals
(end frames 7200–8340), mean frame time was 73.49 ms (13.61 FPS), p95 83.22 ms,
p99 124.30 ms, and maximum 153.13 ms. There is no demonstrated FPS gain or
resolution of prior crashes; prolonged active gameplay remains unqualified.

The existing temporal occlusion policy can still reveal objects late; this
change fixes CPU data ownership, not that policy. Fresh fragment-census data
must not be interpreted as permission to omit zero-sample draws in future frames.
