# Complete query adapter cost on owned map geometry

The experimental typed adapter now copies and compares only the visited stamps
for the validated BSP's cluster count. Blood Gulch needs 30 entries (120 bytes)
and Battle Creek 29 (116 bytes), rather than 256 entries (1,024 bytes) per query.
Full backing-page and alias validation remains in place. Geometry validation
bounds every traversed cluster, and epoch wrap does not clear unrelated stamps.
The guard remains held through publication and the original allocation tail.

This change is **not installed**. The Vita still runs the original-query census
build `b3ace3af…`; the typed adapter still requires the source-lifetime audit and
an inexpensive admission policy before a hardware comparison.

## Correctness and synthetic cost

ASan/UBSan passes the existing complete context/memory/FP comparison, disabled,
alias, source rejection, concurrent worker, owner parking, in-flight invalidation
and budget tests. Ten injected mutations now include the last live stamp on
either side of the page boundary. They still reject without publishing.

The complete Vita-compiled ARM fixture passes **460 exact context/arena/FPSCR
comparisons**, including 20-, 21- and 30-cluster graphs, four rounding modes,
five FPSCR control combinations, epoch wrap and allocation shortages. The stamp
table crosses a page after its twentieth entry. Existing full-state comparisons
and the original tail are retained.

Default-FPSCR synthetic examples, including guard/admission and the original tail:

| Query | Previous adapter instructions | Current adapter instructions | Firmware copy/clear bytes, previous → current |
| --- | ---: | ---: | ---: |
| Seven-cluster traversal | 32,903 | 31,390 | 4,252 → 3,256 |
| Seven-cluster graph, one result | 9,595 | 8,082 | 3,544 → 2,548 |
| 31-cluster traversal | 126,460 | 125,194 | 6,634 → 5,734 |
| 256-cluster traversal | 693,716 | 693,734 | 17,392 → 17,392 |

The full-size case adds a few instructions for the bound and saves no bytes.
The one-result synthetic original still takes only 3,128 instructions, so this
does not justify enabling the adapter for all positive queries.

## Complete calls against map geometry

`test_arm_owned_cluster_runtime.py` runs the same guarded adapter against
unmodified, locally owned Xbox map BSPs at their original guest addresses.
Private stacks remain on permuted guest pages. The original code and adapter
start from identical arenas, including the full map bytes. The fixture compares
the entire resulting arena, context and native FPSCR, including the original
allocation/list tail. Snapshot construction is counted separately.

Blood Gulch and Battle Creek pass **960 complete comparisons**: 24 inputs per
map under four roundings and five FPSCR controls. Inputs sample portal centers,
both sides, points off the portal plane and radii from 0.125 to 1,000. These are
synthetic inputs on actual geometry, not captures of the Vita's query arguments.
Reported allocated-cluster counts come from the original pool after the 64-result
clamp; they are distinct from the diagnostic's unclamped prefix counts.

At default FPSCR, sampled Blood Gulch calls returning 2–3 clusters save about
27–33% of complete-call instructions; those returning 8–15 save 40–41%.
Battle Creek's sampled 2–3 cluster calls save 12–21%. One-result calls remain
mixed: one Blood Gulch sample improves slightly, but others regress as much as
81%; the Battle Creek samples regress 11–120%. Output size is not a sufficient
cost model and is only known after traversal. These samples cannot establish a
hardware eligibility threshold or predict aggregate FPS.

Snapshot construction takes about 93,500 instructions for Blood Gulch and
91,317 for Battle Creek in this fixture, in addition to each query. It currently
runs once per object batch. Kernel latency/contention, caches, real allocator
bookkeeping and firmware copy/clear instructions are not modeled. Copy/clear
bytes are reported separately. No timing or FPS saving is claimed.

The next admission experiment should learn or cheaply estimate work without
repeating the expensive traversal, retain original execution for small cases,
and measure its own overhead. The [physical view comparison](query-work-census-20260916.md#ground-facing-follow-up)
also shows a large FPS difference with nearly unchanged queries per second;
world preparation and rendering submission remain parallel investigation targets.

## Reproduce locally

Keep generated code, map assets and receipts outside the repository:

```sh
python tools/test_arm_cluster_runtime.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/runtime"
python tools/test_arm_owned_cluster_runtime.py --elf "$RESULTS/runtime/runtime.elf" \
    --maps "$MAPS/bloodgulch.map" "$MAPS/beavercreek.map" \
    --cases-per-bsp 24 --fp-controls --out "$RESULTS/owned-maps"
```
