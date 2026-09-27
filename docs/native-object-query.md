# Native object-space collision query

Status: experimental, default off. Initial Vita pod verification passed; ordinary
gameplay performance and broader correctness testing are still required. This is
not a claimed FPS improvement.

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

Production fusion generation now installs this guarded hook automatically when
object-space queries are generated. It also preserves the native solver feature
hook. The generator receipt records both integrations. This prevents a later
regeneration from silently removing manually installed hooks while their runtime
settings remain enabled. See `tools/test_collision_hook_retention.py` for the
owned-input regression test.

For an older private generated stage, install the hook with:

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

- Vita perf250 pod run: 24,080 object-query comparisons, zero mismatches,
  declines or journal failures; the captured pod view was intact. This is a
  stationary workload, not combat or full-campaign validation.
- Pi fast-path attribution: the last ten object-space windows averaged 1.357 ms
  versus 1.781 ms for the fused path. This is supporting evidence only.

Normal perf250 hardware results (mode 2, phase timers off, prior shader and
scene-index changes retained):

| Workload | Mean frame time | FPS | p95 | p99 | Frames over 200 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Standing pod, 1,200 intervals | 56.629 ms | 17.66 | 73.419 ms | 106.237 ms | 0 |
| Standing outdoors, 1,200 intervals | 49.721 ms | 20.11 | 60.021 ms | 79.478 ms | 0 |
| Movement, jump, turn, firing/reload, 1,200 intervals | 51.658 ms | 19.36 | 65.016 ms | 86.768 ms | 2 |

The prior perf247 pod/outdoor/active averages were 17.51/19.97/19.64 FPS.
These small differences do not establish a whole-frame speedup. The active
route is not an identical device-timed replay; host command durations vary.
Keep this option experimental and off by default. The earlier 24,080 hardware
comparisons establish observed query correctness, not a performance benefit.
Normal mode does not compare results and must not be cited as additional
zero-mismatch verification.

The captured outdoor and post-exercise views retain terrain and the weapon;
there was no observed crash or scene abandonment. This short input sequence is
not the required 15-minute active session, NPC combat validation, or full canyon
cutscene coverage. Measurements are CPU Present-to-Present intervals, not GPU
service or physical scanout times. About 26% of settled outdoor intervals still
exceed 50 ms; a 20.11 FPS average does not meet the sustained target.

Next: narrow the remaining scene material-packet preparation cost. Existing
nested profiles put most model-loop time beneath material/draw preparation,
not transform preparation. Preserve packet ownership, original ordering and
callee side effects; do not cache whole packets by material identity.
