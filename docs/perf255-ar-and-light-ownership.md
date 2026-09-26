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
