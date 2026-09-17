# Model hierarchy and palette composition qualification

The existing hierarchy and palette batches are suitable for a guarded cumulative
gameplay trial after explicit startup selection. They already compile into the
retained eighteen-path build, but both remain disabled at runtime. This change
adds focused correctness fixtures; it changes no production algorithms, defaults,
graphics settings, or device files. It makes no FPS claim.

`tools/test_model_batch_composition.py` generates private regions from an owned
matching executable. It executes hierarchy preparation through the final original
child iteration, then passes the resulting matrices into palette preparation.
All four option combinations compare complete contexts at both continuations,
the 4 MiB guest arena, page table, FPSCR, and expired-budget observer snapshots.
The ARM harness links the unchanged retained hierarchy, palette, and math objects.
Matrix NEON remains disabled. The surrounding guest regions are freshly emitted
bounded fixtures, not the complete production callers.

There are 56 cases covering admitted chains/trees, root preparation, short tails,
small models, both scheduling budgets, signed zero, NaN, subnormal and derived
numeric declines, noncontiguous mappings, and aliased outputs. Lazy flag backing
fields start nonzero; x87 stack positions rotate; FCW includes 0x27f and 0x37f.
The existing native leaves are the reference for exceptional cases whose prior
qualification documented original-lift NaN differences. Ordinary cases also
compare against independently emitted original leaves. Numeric admission and
fallback remain unchanged.

`tools/test_model_batch_workers.py` extends the existing real object-worker
fixture, retaining its production pool, lock, private-math and owner-service code.
Four ASan/UBSan processes cover private math off/on and lock profiling/timed waits
off/on with two workers. Each completes 600 hierarchy comparisons, including the
last original child, and 600 owner-side palette comparisons after joining workers.
Half of the hierarchy jobs retain an enclosing transaction. The guard counters
witness both allowed private leaf releases and nested-scope rejections.

The ARM lane models serial lock imports; the host worker test supplies real
concurrency separately. These tests do not prove all game object dependencies,
arbitrary concurrent model mutation, native stack high-water, or hardware speed.
Neither fixture introduces parallel palette consumption before worker completion.

To reproduce with the recompiler Python dependencies, Unicorn, pyelftools, and
VitaSDK available:

```sh
python tools/test_model_batch_composition.py \
  --xbe "$XBE" --manifest "$MANIFEST" \
  --retained-objects "$RETAINED_BUILD/build/recomp" \
  --output-dir "$PRIVATE_EVIDENCE/arm"
python tools/test_model_batch_workers.py \
  --regions "$PRIVATE_EVIDENCE/arm/owned-regions.c" \
  --output-dir "$PRIVATE_EVIDENCE/workers"
```

Keep generated guest code, objects, and receipts outside the repository. The
private qualification evidence is under
`validation/engine-restructure-20260914T2300Z/direct-cluster-query/model-batch-composition-fixture`.

For an authorized fresh-launch trial, preserve the existing configuration and
set only `XV_NATIVE_MODEL_PALETTE=1`, `XV_NATIVE_MODEL_HIERARCHY=1`, and explicitly
keep `XV_NATIVE_MATRIX_NEON=0`. `env.txt` loads first; `xita.cfg` wins afterward.
Remove or update duplicate occurrences of those keys so a later explicit zero
cannot silently disable the trial. Preserve all native-resolution, standard
graphics and retained optimization settings. Restart and verify the loaded
values and positive batch counters during ordinary gameplay.

Neither dashboard nor overlay exposes these internal options. The current remote
updater writes executable slots and has no settings-write API. A persistent file
edit therefore requires filesystem access; updating the executable alone cannot
override the explicit palette zero seen in the current log. A separate startup
override design would require its own review. No activation was performed here.
