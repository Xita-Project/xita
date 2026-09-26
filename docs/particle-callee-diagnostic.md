# Particle callee attribution — perf257

September 26, 2026. Perf256's short ordinary firing test did not establish a
frame-time improvement. Before rewriting more particle math, separate the
inclusive 80720 cost into its own work and its three direct callees.

Perf257 retains perf256 and adds existing `xv_scene_phase_begin/end` observers
around calls from 80720 to 571F0, 57810 and 1721B0. Use owner-only
`XV_SCENE_PHASES=2` for attribution. This is a diagnostic build, not a new
optimization. Timers perturb execution, so its FPS must not be compared directly
with uninstrumented gameplay as evidence of a gain or regression.

The first two calls perform environment/tag lookups and vector work; 1721B0 is
the collision-vector test. The existing native 1721B0 facility replaces its
88E90 BSP ray-cast subtree, not the entire collision operation. Object traversal
and other guest work remain. See [the native boundary](native-1721b0.md).

Reproduction on a private clone of the perf256 stage:

```sh
python3 tools/patch_scene_phase_timers.py STAGE/recomp \
  --parents 00080720 --any-call
```

Verification compared every translated shard before and after: only 80720
changed; removing the three observer pairs makes the shards identical. The
patcher's two host tests passed, and the full developer Vita build succeeded.
Private receipts and test scripts are in `particle-callee-diagnostic/` beside
the source checkout. Generated code and game inputs are not committed.

Compatible package runtime SHA-256:
`892d78f11fa03e4d215420b0c9d5c05e28de0225192beafb260c9a8c781f22a4`.
Only `game-a.self` and `boot-game.txt` are replaced in the preceding compatible
package; the asset contract is unchanged. Deployment and hardware attribution
are pending. Use the protected `a30-perf211` save namespace and inspect the
settled lifepod view before running the short idle/fire/cooldown sequence.

Interpretation: parent minus direct timed children is residual inclusive wall
time, including scheduling and timer overhead; it is not exact CPU self time.
Reject incomplete/overflowed scopes. Compare call counts and inclusive shares
across idle/firing/cooldown windows to select the next implementation target.
No claim about the Warthog/rocket crash follows from this test.
