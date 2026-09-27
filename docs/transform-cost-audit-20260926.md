# Remaining 8DDF0 transform work

Perf267 remains installed with scene-helper portal clipping enabled. This audit
uses the existing ARM Linux whole-game harness on Pi cores 0/1, not Vita FPS
measurements. The underlying point-location harness is older than perf267.
Both bounded runs completed their planned 120-second timeout (124), with 39
host reports. No background Halo 2 compute process was observed before launch.

## Measured native and child costs

The first probe wraps the actual 8DDF0 body and its existing hierarchy/basis
hooks with per-thread counters, leaving guest execution and callbacks intact.
Across 199 complete groups (815,104 calls): total inclusive wall time was
3,643,955 us; hierarchy 432,919 us; basis 262,332 us. Both native paths were
active (positive hierarchy batch reports, all 703,136 observed basis calls
accepted). Together the two hooks account for about 19% of this probe's total.
The rest includes guest descendants and instrumentation, not just wrapper code.

The second probe additionally times immediate guest children. Across 198 groups
(811,008 calls), total was 5,615,234 us. Largest child totals:

| Child | Inclusive us | Calls |
|---|---:|---:|
| B5B40 matrix multiply | 932,692 | 2,348,542 |
| B5F60 quaternion matrix | 407,206 | 932,398 |
| A4CE0 | 381,300 | 209,483 |
| 90770 (existing native aim path) | 332,756 | 213,660 |
| B5EA0 | 298,192 | 811,008 |
| A1E40 | 237,906 | 490,127 |

The per-call total rises from 4.47 to 6.92 us with additional child clocks.
This is a warning about observer overhead, not an engine regression. Do not
interpret the remaining 2,368,346 us in the second probe as purely removable
translated instructions, or project these times onto the Vita. The hardware
phase capture and this supporting run both identify matrix multiply as a
substantial descendant; its native implementation already exists.

## Reference-code interpretation

The local bnunu-halo-1 reference's `object_compute_node_matrices` describes
animation state, overlays, parent attachment transforms, root transforms and
child hierarchy composition. Its caller's no-recompute bit is an engine rule,
not permission to skip arbitrary unmoving models. Position-only caching could
freeze animation or attachments. Reference layouts/version must still be checked
against our owned executable; these names are guidance, not binary equivalence.

The current generated 8DDF0 contains a root path with consecutive B5B40 calls at
8E4F4 and 8E500, followed by a common final multiplication at 8E586. The first
two write the same output matrix, and the last call overwrites math/register
scratch. Existing hierarchy batching handles child nodes and deliberately keeps
the root/final iteration original. This makes the root composition sequence a
candidate for a bounded native fusion, not another hierarchy implementation.

Before integrating it: establish this path's frequency, pin the exact caller
region, validate input/output aliases and stack layout, preserve every float
rounding point and intermediate scale, then compare the full enclosing ARM
consumer including context, guest stack and FP status. Keep the last original
call if needed to retain scratch semantics; never assume dead scratch without
consumer evidence. No fusion implementation or speedup is claimed here.

Private evidence: `../transform-native-phase-pi/` and
`../transform-child-phase-pi/` contain modified private generated units, patch
receipts, build scripts and summaries. Raw logs are in
`../d3d-record2-work/pi-runs/codex-transform-{native,child}-split-20260926.log`.
No proprietary/generated source has been added to Git. The 20-FPS goal remains
unmet, particularly firing, the lifepod and the canyon cutscene.

## Follow-up: common root pair and isolated ARM prototype

A call-site counter probe (no per-child clocks) observed 794,624 object calls:

| Matrix call site | Calls |
|---|---:|
| 8E374 | 496 |
| 8E419 | 685,446 |
| 8E4F4 | 16,053 |
| 8E500 | 16,053 |
| 8E520 | 669,393 |
| 8E586 | 913,566 |

The initially proposed 8E4F4/8E500 pair is rare. The candidate now targets
8E520 followed by 8E586: about 84.2% of observed object calls. This count is
from the supporting Pi harness, not a frequency guarantee for every Vita scene.
Evidence: `../transform-sites-pi/summary.json` and the corresponding
`codex-transform-sites-20260926.log` in `../d3d-record2-work/pi-runs/`.

A private prototype combines admission and stack/register handling around the
two existing snapshot multiplies. It preserves both arithmetic operations and
their intermediate float rounding. It does not cache animation or skip models.
On the Pi, 1,024 differential cases passed exact full-arena, full-context and
FP-exception checks against the two existing native leaf calls plus their
intervening caller instructions. Cases include four rounding modes, all x87
stack positions, finite values, signed zeros, subnormals, infinities and NaNs.
The x86 host version initially failed a NaN-payload comparison; the ARM helper
uses explicit operand order and passed without normalization. This is not yet
an enclosing generated-consumer equivalence test.

An isolated ARM microbenchmark, pinned to Pi core 0, ran four million pairs per
implementation in alternating one-million-call groups. Original pairs took
310.735–310.909 ns, combined pairs 187.590–187.652 ns (about 40% less for this
isolated operation). This fixture excludes production worker locking and does
not prove any Vita frame-time gain. It also uses repeated fixed inputs; cache
and workload behavior differ from ordinary gameplay.

Private source, executables and receipts: `../root-compose-candidate/`.
Nothing is integrated into generated call sites or deployed; perf267 remains
the hardware build. Before integration, qualify the owned caller and enclosing
consumer, worker ownership/guard release, disabled and optional math modes,
and concurrent use. In particular the prototype retains its guard across both
operations, whereas the existing leaf can release the guard for private output.
Do not introduce serialization while reducing setup cost. Hardware gameplay
validation remains required after those checks.
