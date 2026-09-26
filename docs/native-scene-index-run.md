# Ordered scene index scan prototype

Status: host and Pi checked; opt-in perf244 hardware qualification pending.

Perf243 attributed approximately 3.6–4.1 ms of diagnostic elapsed time to
`54010` itself, after timing its indirect callbacks separately. One inner loop
walks consecutive signed surface indices below a material boundary. This is a
candidate for reducing repeated address translation and guest register/flag
updates; it is not a replacement for the complete dispatcher.

`recomp/kernel/xk_scene_index_run.h` consumes a prefix of that loop. It scans
ordinary RAM within one mapped page, without writing guest memory, and updates
the exact final registers, lazy flags and preemption budget. It stops before
the original loop's exit, a page boundary, or a scheduler handoff. The original
code still executes those events. Prefixes shorter than four entries decline.
MMIO addresses, invalid mappings and context/arena aliases decline too.

Future integration must use the active scene helper's page table and arena,
establish ownership of the input list during the prefix, and exclude contexts
or diagnostics whose memory-access observations would be bypassed. The helper
itself does not establish thread ownership. There is no production hook yet.

## Correctness evidence

The comparison tool extracts the audited inner loop from an owned retained
shard, checks its SHA-256, and writes the generated reference only to a private
output folder:

```sh
python3 tools/test_scene_index_run.py \
  --reference /private/recomp/code_009.c --out /private/new-index-test
```

Host ASan/UBSan and ARM Thumb builds each passed 2,400 full context, memory and
mapping comparisons: 5,984 admitted batches and 8,678 matching yield states.
Cases include signed thresholds, short runs, page crossings, varied budgets,
and changes to the bound, threshold, stack location, input values or mappings
at an original scheduler handoff. Nine explicit declines preserve context.

A separate Pi campaign harness predicted a prefix into a copied CPU context,
let the original loop execute, and compared its state at the corresponding
back-edge. It finished its planned 120-second run with 356,352 prefixes checked,
zero mismatches, and a final report at frame 2255. This exercises real inputs
without applying the candidate's result. It does not replace hardware checks.

## ARM cost and workload evidence

Isolated Pi measurements, including common fixture setup, in ns per call:

| Entries | Original | Candidate |
|---:|---:|---:|
| 1 | 49.0 | 59.5 |
| 2 | 55.1 | 82.4 |
| 3 | 62.4 | 106.3 |
| 8 | 99.0 | 97.9 |
| 32 | 284.8 | 147.4 |
| 128 | 983.6 | 312.3 |
| 512 | 3788.7 | 963.2 |

Short runs regress, so distribution matters. A separate original-path a30
capture after frame 1800 contained 123,904 runs and 21,221,267 index steps in
121 complete report groups: 171.27 steps per run, 87.47% longer than eight
entries, and 37.47% longer than 128. These are host workload observations,
not proof of Vita time saved. No collected settled report groups were rejected;
the parser rejects interleaved or incomplete lines rather than accepting them.

Private receipts are in `scene-index-run-host-v2/pi-isolated-result.txt`,
`scene-index-profile/summary.json` and `scene-index-verify/summary.json` beside
the authoritative source checkout. An earlier v2 timing run overlapped the
campaign harness; use the isolated timing receipt above instead.

Next: add an opt-in helper-owned integration, verify on actual hardware, then
measure normal gameplay with diagnostics disabled and existing improvements
retained. Keep the original loop as the fallback. Do not infer FPS from Pi cost.

## Guarded integration

`tools/patch_scene_index_run.py` installs a hook only after checking the retained
loop hash. Removing its include and three hook statements restores the original
shard byte-for-byte. Reapplication is idempotent and layout drift is rejected.

`XV_SCENE_INDEX_RUN=1` predicts a prefix, retains original execution, and compares
the complete context at the corresponding back-edge. Mode 2 applies the prefix;
absent/other settings leave the original loop active. A mismatch is logged and
disables the hook for that dispatcher invocation; it never replaces guest state
in verification mode. Checked-address builds decline the hook entirely.

Admission currently requires the active scene helper's copied context and its
allocated 256 KiB private stack. Both the index page and complete bound word must
belong to that stack. Global scene lists remain original-path until their
ownership is separately established. It uses the dispatcher's actual cached
arena/page table, including the render view, and never skips an original yield.

Both hook modes passed the 2,400-case suite under host ASan/UBSan and Pi Thumb A9.
The admission fixture additionally checks foreign callers, out-of-stack inputs,
wrapped addresses, a bound word crossing the stack end, and checked-build
exclusion. Hardware verification and normal gameplay measurements remain required.
