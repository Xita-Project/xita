# Call-local sampler translations — 2026-09-27

Candidate only, disabled unless XV_MATERIAL_SAMPLER_POINTERS=1 is explicitly
compiled. Perf273 remains installed; no hardware speedup is established.

The existing ordered sampler helper repeatedly translates the same two stack
slots and texture-state page. The candidate resolves those pointers once per
helper call, retains every stack/table store and argument read in original
order, and retains intermediate register updates. It is not a persistent cache
and does not skip state writes or reorder draws. Stack spans crossing a page or
wrapping address zero and guest spans overlapping the host context use the old
path. Checked-address builds also retain the old per-access path.

## Qualification so far

Private receipts: ../sampler-pointer-candidate/.

- Host ASan/UBSan and Cortex-A9 Thumb ARM/Pi fixtures each passed 16,384 complete
  context/64 KiB memory comparisons against the retained ordered helper.
- Cases vary page mappings between calls, distinct virtual pages sharing physical
  memory, stack/table overlap, byte alignment, page boundaries, wrapped virtual
  addresses and table/context overlap. No real concurrent remapping is modeled.
- Pi CPU0 timing: baseline/candidate/candidate/baseline, two million groups each,
  140.317 / 64.497 / 65.623 / 146.609 ns per group. This is an isolated noinline
  helper microbenchmark, not a predicted frame-time reduction. Production caller
  inlining, memory traffic and contention can change the result.
- The retained generated-instruction fixture completed under ASan/UBSan: 4,096
  context/memory cases each for owner and helper admission, plus diagnostic
  fallback, all passed with the candidate enabled. This uses mocked ownership
  and guest mapping; it does not establish real thread scheduling safety.

## Remaining gates

Review mapping lifetime guarantees for admitted owner/helper calls, including
render-view watchdog behavior. The pointer shortcut assumes mappings remain
stable within this no-call/no-yield sequence; it must not weaken existing memory
ownership requirements. Qualify Vita compiler output and production integration,
then add a tracked build switch and measure ordinary gameplay before promotion.
No Makefile default or currently installed build was changed. Preserve the other
qualified optimizations and the a30-perf211 save namespace.

## Lifetime review: narrow the shortcut to stack slots

`xv_render_view_mirror` preserves active shadow/image-copy entries, but
`xv_render_view_watchdog` explicitly restores mappings from another thread.
Therefore the initial table-pointer shortcut is not promoted. The revised
candidate translates the texture destination at each original store. Only the
8-byte stack span is retained; image-page stack addresses decline as well.
The normal overlapped helper stack is a kernel-owned high allocation made once
in scene configuration (`xk_scene_thread.c`), outside the low physical pages
shadowed by render view. The sequence has no guest calls/yields and does not
retain pointers after returning. Existing object-worker/diagnostic admission
rules remain unchanged. This does not add safety to the pre-existing emergency
watchdog race; it avoids introducing a stale image pointer across that recovery.

Revised stack-only results:

- Host and Pi: 16,384 exact full-context/memory cases each, including an injected
  texture-page remap between individual stores. This models observable remapping,
  not a full concurrent watchdog execution. Receipts remap-host.log/remap-pi.log.
- A mutant restoring the cached texture-table pointer fails at stage 0 case 1036;
  the new coverage detects the specific stale-mapping behavior.
- Stack-only Pi baseline/candidate/candidate/baseline measured
  142.177 / 84.906 / 84.838 / 144.306 ns/group, before adding the image-page
  decline and remap-injection fixture. This remains a supporting microbenchmark.
- Retained generated-instruction fixture: owner/helper 4,096 cases each passed
  with stack-only translation, plus diagnostic fallback (stack-guest.log).
- VitaSDK compiled the final remap fixture to an ARM object. No linked gameplay
  executable or Vita runtime equivalence claim follows from that compile.

Makefile integration now offers XV_MATERIAL_SAMPLER_POINTERS=0 (default) or 1,
requires native sampler enablement, and tracks flag changes for xd3d.o only.
`tools/test_sampler_pointer_build.py` verifies repeat builds, 0→1→0 transitions,
unaffected unrelated objects, and rejection of invalid flag combinations.
Next step: isolated Vita build retaining perf273 settings and generated code,
then ordinary gameplay measurement. Perf273 remains installed; no update yet.

## Perf275 build started

Private stage ../sampler-pointer-hardware/ is a reflink of perf273 with only
the sampler header, dedicated Makefile option, and build/version identification
changed. It targets perf275 / d0975e0b with XV_MATERIAL_SAMPLER_POINTERS=1.
All prior build settings are preserved; the rejected native-material -Os trial
is not included. Runtime/generated source remains otherwise unchanged.

xd3d.o compiled successfully; xv_material_sampler_try grows from 0x27c to
0x39c bytes. The existing shared-header dependencies also trigger recompilation
of generated code_011.c, which is still running. The preliminary object audit
therefore must be repeated once the build is terminal; it is not final package
qualification. No package has been produced or deployed. Perf273 stays on the
Vita. Launch and ordinary idle/fire/move/outdoor collection scripts are prepared
with version guards and the same 32 overrides, protected a30-perf211 save.

## Perf275 completed package

Full build completed successfully. The final object comparison after compilation
confirms only kernel/xd3d.o differs from perf273; regenerated code_011.o and
all other generated objects are byte-identical. Package membership and assets
are unchanged; only game-a.self and boot-game.txt differ.

Runtime: 34,818,338 bytes; SHA-256
`37a6724045ff7c54d9302e1f11ee052dd6dcada911d69c1901057f5d8e480f40`.
Package SHA-256:
`0e80fc49c56e22e93e4149eb5fb157f6d7b6acc94a5e6531dab5c1992cd22d5d`.
Update contract: `775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897`.

Private package: ../sampler-pointer-hardware/xita-perf275c.vpk. Update/apply
has started; installation must be confirmed by the verified boot receipt and
live version status. No hardware performance result for this build yet.

Deployment completed: verified=true and boot_confirmed=true, slot 1, with the
runtime hash above. Independent live status confirmed perf275 / d0975e0b at
timing_frame=0 on the dashboard. The ordinary a30 launch sequence is now running;
it renews the keep-awake lease before input. Perf273 is retained in slot 0.
No perf275 gameplay measurement is available yet.

## Perf275 hardware results

| Interval | Samples | Mean ms | FPS | p95 ms | Max ms | >50 / >100 ms |
|---|---:|---:|---:|---:|---:|---:|
| pod | 780 | 57.113 | 17.509 | 74.754 | 87.795 | 570 / 0 |
| fire | 68 | 78.240 | 12.781 | 104.576 | 127.425 | 67 / 4 |
| move | 86 | 64.310 | 15.550 | 83.664 | 269.953 | 85 / 2 |
| outside | 934 | 48.469 | 20.632 | 58.856 | 88.338 | 216 / 0 |

CPU Present intervals, same 32 launch overrides and 360p. Pod and outdoor
screenshots were inspected and show the intended scenes. Movement is controlled
by wall time and can end at slightly different positions; these are ordinary
gameplay samples, not deterministic identical-workload replays. No crash was
observed during this short sequence. No AI, audio, cutscene, save/resume or
15-minute stability qualification is implied.

There is no demonstrated whole-frame improvement against perf273 (pod 55.849,
fire 76.515, move 64.206, outside 48.428 ms). Movement/outdoor times are close;
pod/fire are slower in this sample. The isolated Pi gain is insufficient to
claim a Vita frame-rate gain. The pod logs show 5,760 accepted groups per stage
per 60 frames: 384 total groups/frame. That frequency limits the aggregate
payoff of such a short helper. Do not spend another iteration retuning it
without evidence of substantially greater production cost.

Perf275 remains installed provisionally, with perf273 retained for rollback;
the build option stays default-off. No repeated A/B loop is planned. The
269.953 ms movement outlier remains and the full 20 FPS goal is unmet. Next
investigation should address larger material/recording or simulation work,
using existing attribution before proposing another narrow native replacement.
Private captures and summaries: ../sampler-pointer-hardware/. All collection
processes have completed; no gameplay controls are held.
