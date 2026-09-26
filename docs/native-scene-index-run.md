# Ordered scene index scan

Status: opt-in fast path tested on physical Vita in perf246. A modest lifepod
improvement was observed; sustained 20 FPS and full-game correctness remain unmet.

The inner loop at 54132 in dispatcher 54010 walks consecutive signed surface
indices below a material boundary. `xk_scene_index_run.h` consumes a prefix of
that loop within one mapped page, without writing guest memory. It updates the
exact registers, lazy flags and preemption budget. Prefixes shorter than four
entries decline. The original code handles exits, page transitions, callbacks,
and scheduler handoffs. MMIO, invalid mappings and context/arena aliases decline.

## Admission and controls

`xk_scene_index_hook.h` requires the active scene helper's copied context and its
private-stack bound word. Input indices must be on that stack or in CE's bounded
0x4000-entry scene list at 38BE14. The reviewed producer 542F0 constructs that
list before the material passes; the bound may not exceed its capacity. Other
global/heap regions are excluded. The hook uses the dispatcher's active cached
arena/page table, including its render view. Checked-address builds decline.

`XV_SCENE_INDEX_RUN=1` predicts into a copied context and compares against the
original loop at the corresponding back-edge. Mode 2 applies the prefix; mode
0/default retains original execution. Verification reports the first comparison
and each 4,096 thereafter. A mismatch logs and disables the hook for that
invocation; verification never replaces guest state. Fast mode logs activation
once. Vita reports use the critical sink because ordinary helper logs are dropped.

`tools/patch_scene_index_run.py` requires the audited retained-loop hash. Removing
its include and three hook statements restores the original shard byte-for-byte.
Reapplication is idempotent; layout drift is rejected. Literal reference inspection
is supporting ownership evidence, not a universal pointer-alias proof.

## Correctness evidence

Run against owned retained code, writing generated references to a private folder:

```sh
python3 tools/test_scene_index_run.py --hook --global-list \
  --reference /private/recomp/code_009.c --out /private/new-index-test
```

Host ASan/UBSan and Pi Thumb A9 passed 2,400 full-context/memory/mapping cases per
mode, with 8,678 matching yield states. The global-list variant admitted 5,987
fast batches; the stack fixture admitted 5,984. Cases cover signed thresholds,
short/long runs, page crossings, budgets, and changes to bounds, input values,
thresholds, stack locations and mappings at original handoffs. Separate admission
checks cover foreign callers, stack boundaries, capacity overflow and diagnostics.

A Pi a30 run verified 356,352 prefixes with zero mismatches. Physical perf246
then verified **372,736 prefixes with zero mismatches** through the lifepod,
with the scene confirmed in a screenshot at frame6280. These checks establish
observed equivalence for tested inputs, not every possible game state.

Earlier perf244 stderr silence and perf245's coarse report threshold were not
counted as verification passes. Private receipts live beside the source checkout
in scene-index-verify, scene-index-global-differential and scene-index-candidate.

## Cost and physical measurements

The original-path Pi census observed 123,904 runs averaging 171.27 index steps;
87.47% exceeded eight entries and 37.47% exceeded 128. Full-hook Pi measurements
show short-run overhead and long-run gains: 128 entries measured 986.7 versus
413.3 ns/call; 512 measured 3,787.6 versus 1,054.4. These include fixture setup
and are not Vita FPS predictions.

Normal perf246 with earlier qualified optimizations retained:

| Qualified view, 1,200 intervals | Mean ms | FPS | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| Lifepod, frames6300–7440 | 56.440 | 17.718 | 73.000 | 89.404 | 133.626 |
| Outdoors, frames11040–12180 | 56.022 | 17.850 | 68.574 | 99.928 | 139.767 |

Earlier pod runs measured 58.15–58.73 ms; the outdoor reference was 56.52 ms.
The outdoor difference is too small to claim a clear improvement from one run.
Both windows had zero intervals over200ms. A short firing/turning/movement check
completed without a crash; it does not satisfy the 15-minute active combat gate.
