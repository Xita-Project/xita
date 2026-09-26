# perf255: AR cost and light-list ownership

September 26, 2026. Physical Vita, developer perf255, ordinary a30 gameplay in
`XV_TEST_SAVE=a30-perf211`. Runtime SHA-256:
`cb6e797033a50e1e48d09298f7d2b512ae62545a24a699161aa224a0d4e12e37`.
The diagnostic build retains perf254 gameplay code. This is not a build-to-build
speedup claim or a completed stability test.

## AR firing at the lifepod exit

A cold launch resumed the protected test save. Menu inputs were sent as a
sequence; screenshots verified the lifepod and the AR counter changing from
60 to 8 rounds. No automated benchmark mode was active. Resolution was 360p,
with the retained native/material/index optimizations and deferred recording.
The launch environment is archived privately with the logs and screenshots.

| Phase | Samples | Mean ms | FPS | p95 ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Before firing | 152 | 53.910 | 18.55 | 68.499 | 108.581 | 112.805 |
| Firing | 58 | 78.296 | 12.77 | 98.635 | 182.904 | 182.904 |
| Cooldown | 128 | 63.628 | 15.72 | 83.508 | 115.847 | 119.464 |

The periods were approximately 8.19 / 4.56 / 8.13 seconds. Every Present interval
in the marked ranges was available; joins use `/status.timing_frame`, not the
remote image counter. Input/status boundaries bracket the activity rather than
identifying exact weapon simulation ticks. Screenshots were outside these
periods. Firing added about 24.39 ms/frame here, with a lingering cooldown cost.
There were no scene timeouts in the captured run.

The surrounding 60-frame reports support investigating the owner update first:
FA920 inclusive elapsed rose from about 40.55 to 65.08 ms/frame. Owner wait on the
scene helper did not rise (3.23 to 2.47 ms); snapshot merge stayed near 1.8 ms.
Draw count rose from 274 to 320, pump elapsed from 9.1 to 13.5 ms, and deferred
recording drain waits from 0.51 to 2.28 ms/frame. These overlapping counters and
report windows are NOT additive CPU-self costs or exactly the marked intervals.
They do not rule out contention or GPU costs. Next: enable the existing owner-only
phase timers (`XV_SCENE_PHASES=2`) on a cold launch and identify the child work
behind FA920's increase before choosing a replacement or scheduling change.

## Render-side light mutation

A static direct-call audit of the matching private generated code identifies:

- `BCB30 → 5DBC0 → 5D990 → 5D410 → 92890 → 92330 → 56670`.
- `BCB30 → 5DBC0 → 5D990 → 5D410 → 5B710 → 5B4A0 → 5AE10 → 92120 → 91EE0`.

`92890` can unlink light references through `565E0`, delete a light datum through
`A92C0`, and update other lights through `92330`. `56670` allocates and links
references. Thus render traversal reaches mutation as well as the list reader
found in the Warthog/rocket dump. This is structural reachability, not proof that
these branches executed concurrently in that crash. Conditional paths and
indirect calls still require runtime evidence.

The snapshot merge resolves conflicts word by word. Linked-list records, heads
and allocator metadata have joint invariants; independent word conflict handling
alone does not establish that those invariants survive concurrent allocation,
removal and slot reuse. Investigate that ownership boundary with the perf255
freeze capture. Do not silently skip lights, disable effects or discard all scene
writes as a presumed fix.

## Owner-only phase follow-up

A second cold launch enabled `XV_SCENE_PHASES=2`. The first firing sequence was
interrupted near the cutscene-to-gameplay transition and explicitly marked
invalid for comparison. The completed follow-up was at the settled lifepod
view, after reloading; screenshots show the magazine changing from 60 to 10.
Private evidence: `freeze-light-candidate/ar255-phases2-*`.

The marked ranges are 6796–6922 (idle), 6923–6976 (fire), 6977–7085 (cooldown).
The phase reporter uses 60-frame windows, so window 6900 is wholly idle while
6960 mixes idle/firing and 7020 mixes firing/cooldown. Selected inclusive timings:

| Path | Window 6900 ms/frame | 6960 | 7020 |
| --- | ---: | ---: | ---: |
| FA920 → simulation 109760 | 47.53 | 52.08 | 59.04 |
| FA920 → real-time update 108FD0 | 11.41 | 14.93 | 18.25 |
| 108FD0 → particle update 10E7A0 | 5.84 | 9.30 | 12.46 |
| 10E7A0 → particle 10E240 | 4.92 | 7.66 | 11.22 |
| 10E240 → motion/collision 80720 | 3.90 | 5.91 | 6.44 |
| 108FD0 → sound update 2BB70 | 5.51 | 5.53 | 5.72 |

Simulation calls per 60 rendered frames rose 109 → 113 → 117. Thus both added
work per simulation update and more simulation updates per rendered frame need
consideration. Timers materially perturb runtime (the idle marked period was
65.06 ms with them, versus 53.91 ms in the earlier uninstrumented sequence).
Do not use these numbers to predict an optimization's FPS gain or add nested
rows together. No depth-overflow report or scene abandonment was found in the
captured log. The targeted test still does not meet the 15-minute stability gate.

The particle path is a bounded next native-replacement investigation:
`80720` has three direct callees (`571F0`, `57810`, `1721B0`), no `X_CALL`
macro sites in the inspected body, and extensive x87 motion arithmetic. Its
collision callee already benefits from native BSP traversal; replacing it again
would duplicate existing work. `10E240` also handles lifecycle/deletion and has
more callees, while projectile update `C0EA0` is substantially larger.

Next implementation boundary: inspect and time 80720's body versus its three
callees on the host/Pi, then prototype only the evidenced expensive portion.
Preserve float-store rounding, guest-visible writes, register/flag results and
collision outcomes in differential tests. Keep particle counts, collision and
lighting behavior unchanged. A win must be confirmed in the ordinary hardware
firing sequence with detailed timers off. Light-list corruption investigation
remains separate and unresolved.
