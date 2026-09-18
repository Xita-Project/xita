# Copied visibility jobs on the existing workers

`XV_NATIVE_VISIBILITY_JOBS=1` adds a bounded native classification operation to
the existing object worker backend. The two existing worker threads handle two
partitions and the guest owner handles a third. On Vita those workers retain
their existing core 0/1 affinities. This operation executes only the qualified
subcluster math; it cannot call guest functions, request owner services, submit
draws or retain guest pointers.

**The game does not call this interface yet.** This is a qualified queue and
FP-state handoff for the next visibility-pass integration. Its repository
default is off. The earlier cumulative runtime `73c30a34` remains the package
ready for hardware; this work has not replaced or installed it.

## Why batch across clusters

The owned-map audit found these structural sizes:

| Map | BSPs | Clusters | Subcluster boxes | Largest cluster | Surface references |
| --- | ---: | ---: | ---: | ---: | ---: |
| Blood Gulch | 1 | 30 | 181 | 14 | 6,325 |
| Battle Creek | 1 | 29 | 86 | 28 | 3,749 |
| Pillar of Autumn | 9 | 301 | 6,075 | 340 | 364,855 |
| Halo | 2 | 77 | 1,626 | 138 | 65,175 |

These are totals in the owned files, not per-frame draw or visibility counts.
In particular, Blood Gulch has no single cluster with 16 boxes. A worker wake
for every cluster would spread very small jobs across many synchronization
points. A bounded packet spanning multiple visible clusters is the next target.
Surface publication remains on the owner and in original order.

The owned executable audit now also pins all three caller backedges: surface,
subcluster and visible-cluster loops. For validated nonnegative bounded counts,
`8 * subclusters + surface_references + visible_clusters` is a conservative
upper bound on the original pass's scheduling debits. It includes seven for
each non-broad-rejected box. An initial whole-pass fast path can require enough
remaining budget for that bound and otherwise retain the existing path.

Counting each Blood Gulch cluster once gives a bound of 7,803; Battle Creek
gives 4,466. The default refill is 20,000, but neither the actual entry budget
nor a unique visible list is assumed. Runtime capture must count actual visible
entries, including repetitions, and must handle the surface-cap early exit.

## Queue and lifetime contract

- Admission requires the registered current owner/fiber, idle queue, no owner
  pass or service activity, and no stop, native job, phase, census or tracing
  activity. Invalid ownership declines before dereferencing caller pointers.
- The owner copies up to 512 native frustum/box pairs into private fixed
  storage before waking any worker. Finite coefficients, ordered bounds and
  masked FP exceptions are required. Output is untouched on decline.
- Each lane uses the owner's FP environment and restores its own complete
  environment afterward. The owner also restores its environment. Results
  occupy separate cache-line-aligned slices.
- Workers use the existing wake/done semaphores, with a separate pure-data
  dispatch mode. They do not publish object-service notifications. The owner
  consumes every completion before publishing output or allowing another
  batch. Queued guest callbacks and native visibility jobs do not overlap.
- A drained owner report records batch count and items per lane under
  `[visibility-jobs]`. Counts are not CPU utilization or elapsed-time evidence.

The enabled ARM object adds 1,188 bytes of text and 61,696 bytes of BSS relative
to the retained backend; data size is unchanged. No additional threads, guest
stacks or per-batch allocations are introduced. The caller must still supply
valid native arrays; this interface does not validate arbitrary native pointers.

## Qualification and remaining work

The actual pthread backend passes **779 calls/declines** with both
address/undefined-behavior sanitizers and ThreadSanitizer. This includes 705
successful packets, zero/one/two active workers, 14 packet sizes up to 512,
four rounding modes and four x86 denormal-control combinations. Serial results
match; input/guest context/arena/page table and owner FP state are preserved.
Both workers' saved FP state is checked across subsequent batches.

The test alternates 32 original object callbacks with native jobs. It also
changes the caller's original input array while a worker is deliberately held:
results still use the private copy, output remains unpublished until the join,
and a foreign thread cannot inject another job or reset live counters.

Five VitaSDK build transitions preserve byte-identical disabled objects and
reproducible enabled objects. Nine invalid settings/dependencies reject.
The owned-map shape audit covers 24 maps and 82 BSPs. These host/compile results
do not establish Vita scheduling speed or an FPS gain.

Next implement the bounded visibility-pass capture and owner publication:
validate source spans and physical aliases, copy the actual visible records,
account for exact original budget/early exits, and preserve unsupported/yielding
cases through the existing implementation. Compare the complete visibility
consumer before enabling the caller in a cumulative build. Existing pure math
qualification need not be repeated unless that math changes.

Private receipts live in `direct-cluster-query/visibility-jobs-20260918`,
including `asan-qualified`, `tsan-qualified`, `make-qualified.json`,
`caller-contract.json` and `workloads-qualified.json`. The device responds to
ICMP but the configured Xita service does not accept a TCP connection. No new
runtime has been uploaded. Stable 20 FPS still requires ordinary hardware
gameplay verification.
