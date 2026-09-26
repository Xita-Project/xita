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
package; the asset contract is unchanged. Deployment verified the runtime hash
and confirmed the new boot in slot 1; perf256 remains in slot 0 for rollback.
The protected a30 launch and short idle/fire/cooldown sequence completed. The
before/after images show the same lifepod view and AR ammunition 60 to 14. All
marked frame intervals were present. No depth-overflow or nonzero abandoned
scope reports were found in the capture. This is not long-session stability
or a Warthog/rocket reproduction.

Interpretation: parent minus direct timed children is residual inclusive wall
time, including scheduling and timer overhead; it is not exact CPU self time.
Reject incomplete/overflowed scopes. Compare call counts and inclusive shares
across idle/firing/cooldown windows to select the next implementation target.
No claim about the Warthog/rocket crash follows from this test.

## Hardware attribution

Input brackets: idle frames 6396–6511, firing 6512–6560, cooldown 6561–6677.
The 60-frame phase reports below deliberately distinguish wholly idle/cooldown
windows from mixed ones. Times are inclusive ms per rendered frame.

| Routine | 6480 (idle) | 6540 (idle/fire) | 6600 (fire/cooldown) | 6660 (cooldown) |
| --- | ---: | ---: | ---: | ---: |
| 80720 total | 5.94 | 4.52 | 6.69 | 5.65 |
| 80720 → 1721B0 collision | 2.60 | 2.21 | 3.58 | 2.75 |
| 80720 → 57810 environment | 1.78 | 1.20 | 1.53 | 1.52 |
| 80720 residual | 1.56 | 1.11 | 1.58 | 1.38 |
| 10E240 → 1746F0 impact handling | not reported | 0.63 | 2.79 | 0.63 |
| FA920 → 109760 simulation | 54.38 | 62.00 | 56.72 | 52.73 |
| FA920 → 108FD0 realtime update | 14.68 | 12.85 | 17.94 | 14.60 |

The direct 80720 → 571F0 branch was not reported; 57810 itself calls 571F0,
so this does not mean 571F0 does no work. 1721B0 calls per 60-frame window were
13,205 / 9,420 / 12,832 / 11,562 respectively. The additional calls relative to
80720 entries in the impact windows agree with its retry/back-edge path.

Measured diagnostic frame means were 73.27 / 89.89 / 74.36 ms (idle/fire/cooldown),
with p95 89.16 / 119.58 / 99.82 ms. Those numbers include substantial timer
overhead and are **not** a regression measurement against perf256. The full
distributions are private `ar257-summary.json`; counts are 116 / 49 / 117 frames.

Decision: replacing 80720's remaining math alone has a small measured ceiling.
Prioritize its environment/collision callees and impact-triggered work, while
keeping the larger simulation path in scope. Static inspection establishes
that 1746F0 calls 112070 and 2B610 and constructs effect/sound inputs; it is not
a disposable visual-only loop. Skipping it could remove gameplay feedback.
Likewise, caching 57810 solely by cluster is not justified: its path consumes
position/flags and follows environment/tag data. Any replacement must preserve
these dependencies and callee-visible state, with differential ARM validation.

After archiving the diagnostic capture, Xita was restarted to remove the
launch-only timer environment. A normal protected launch with
`XV_SCENE_PHASES=0` completed; status showed perf257, advancing timing frames,
benchmark mode off and an active keep-awake lease. No performance optimization beyond perf256
was introduced, and the 20 FPS goal remains unmet.
