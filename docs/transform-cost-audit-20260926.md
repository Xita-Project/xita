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
