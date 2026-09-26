# Native object-space collision query

Status: experimental, default off. Hardware verification and ordinary gameplay
performance testing are still required. This is not a claimed FPS improvement.

The perf249 Vita diagnostic found about 18.4 ms/frame in the movement-collision
routine `172BF0` in the a30 pod. These elapsed timers include waits, preemption
and profiling overhead. The Pi narrowed its corresponding path through
`171F10 → 1716F0 → 172F40`, the object-space collision query. The existing native
`88110` replacement was hooked into the world query, but the object-space entry
still used the fused translation.

`xv_native_4b9d0_object_query` reuses the native implementation and its existing
layout/table/helper-thread admission checks. `XV_NATIVE_OBJECT_QUERY` selects:

- `0` (default): run the original object-space fused query.
- `1`: compare native and original results using the existing write journal,
  context, memory-region and back-edge checks; retain the original result on a
  completed comparison. Treat any mismatch or journal failure as failed validation.
- `2`: use the native query when admitted, otherwise the original object query.

The existing query-part mask still applies. Normal saves and rendering paths are
not changed by this hook. It introduces no new thread or synchronization policy.
The original native query's deferred back-edge accounting and admission limits
are retained; broader concurrency correctness is not established by a single
stationary comparison run.

The world and object entries must use different reference callbacks. In particular,
an object query must retain `nq_run_impl(c, 0)` on fallback and in verification,
rather than the world-specialized `nq_run_impl(c, 1)`. The shared dispatcher accepts
the correct callback instead of selecting a route from a guest return address.
The world entry retains its existing mode and reference. No collision arithmetic,
precision, flags, memory writes or query ordering was intentionally changed.

Install the hook into an existing private generated stage with:

```sh
python3 tools/patch_native_object_query.py /path/to/stage/recomp
```

The patch is idempotent and rejects changed/ambiguous anchors. Rebuild
`query_fusion.o` and `xk_native_4b9d0.o`; do not regenerate a game merely to install
this hook. The stage requires the existing native collision unit and object-space
query support. `test_native_object_query_hook.py` compiles the four native/world-run
configurations and checks routing, drift rejection and idempotence.

Initial evidence, September 26:

- Host differential tests at both `-O2` and `-O0`: 1,000 cases each for the world and new object
  entry, zero mismatches or verify failures; 26 nonterminating reference cases
  were skipped by the existing alarm. These skipped cases are not validation.
  The existing comparator permits differing NaN payloads; the debug run recorded
  one such word. All other checked state and memory matched.
- Pi a30 runtime: 43,982 object-query comparisons, zero mismatches, declines or
  journal failures. World queries remained native mode 2. This validates that
  observed workload, not the entire campaign or Vita timing.
- Verification timings include journaling, allocations and both executions;
  shared native timing counters also include world queries. Do not interpret
  them as the speed of the object-only fast path.

Next: verify on the Vita, then cold-launch with mode 2 and phase timers off.
Measure pod, outdoor movement and NPC combat against the retained normal build,
including slow frames and rendering/collision correctness. Keep the earlier
shader and scene-index improvements enabled.
