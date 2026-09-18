# September 18 hardware completion timing

The user reported roughly 15 FPS looking down Blood Gulch's valley. The
installed build was verified through both status and the updater's boot hash:
`0.2.0-test.2 / 5206ca9`, executable
`ee33c8209b387d35ad112ad0c6f0b1272ce85d9bce2d4131fea9ebad4e00e4a2`.
Recent 60-frame windows include 15.2 and 15.9 FPS, followed by sustained
14.0–14.4 FPS windows. This is ordinary gameplay, not a controlled route
comparison, and does not identify which optimization produced the improvement.

The startup log confirms native 960×544, experimental triple buffering, and
shadows, reflections, cosmetic effects and temporary decals enabled. A request
for 500 MHz failed; the effective CPU clock was 444 MHz, GPU 222 MHz. The model
palette/hierarchy trial was also enabled by existing startup configuration.
Reported AI and shadow defects remain unresolved, so these observations are
not acceptance of complete gameplay fidelity.

## Diagnostic deployment and evidence

The prepared `0.2.0-perf.1` packet-timing executable was uploaded, verified and
boot-confirmed in slot 1 on the physical Vita. Its executable SHA-256 is
`6fa41d2711db9ff247d84522095dce1651422a44194547019a3dfa683c80f6a8`.
Its package differs from test.2 only in `game-a.self` and `boot-game.txt`;
shader assets, Halo 2 executable and the shared asset contract are unchanged.
This adds measurement overhead, not an optimization. The prior executable
remains in slot 0 for rollback.

Blood Gulch was launched through Multiplayer → Split Screen using the existing
profile and Slayer. Captures confirm the dashboard, original menu flow and
rendered outdoor game. No built-in benchmark was invoked. Timing windows below
each contain 60 valid packets, zero failed/invalid packets and a failed poll
before the first successful poll for every packet.

| Packet tickets | Submit mean | Completion bound from submission start | Remaining bound after submit | Mean uncertainty |
| --- | ---: | ---: | ---: | ---: |
| 781–840, menu | 0.827 ms | 26.107–26.501 ms | 25.281–25.674 ms | 0.393 ms |
| 4861–4920, gameplay | 4.288 ms | 51.656–53.126 ms | 47.368–48.838 ms | 1.470 ms |
| 5341–5400, gameplay | 3.276 ms | 54.615–55.927 ms | 51.339–52.651 ms | 1.312 ms |

The latter gameplay windows reported zero newly decoded textures. Individual
observation gaps reached 20–24 ms, so the maxima must not be represented as
uniformly precise. Nevertheless, the conservative mean *lower* bounds still
show substantial outstanding completion latency after CPU submission returns.
Polling delay alone cannot explain it. These are CPU-visible completion bounds,
not GPU service-time measurements, and do not separate fragment work, vertex
processing, memory traffic or driver dependencies.

## Next experiment

Prioritize a matched, warmed outdoor view with a resolution-only change and
these same packet bounds. Keep clocks, shaders and other settings fixed; record
the effective resolution after restart if required. Compare frame interval,
draw preparation, submission and completion together. A reduced completion
bound would implicate pixel-dependent work; a flat bound would redirect the
investigation toward geometry and scene dependencies. Do not repeat the prior
unhelpful global half-precision shader trial or infer a speedup from different
views. Return to the ordinary build for final FPS acceptance.

Private evidence is preserved under
`/home/birchwoodgod/xita-backups/2026-09-18-unified-games/valley-followup/`:
baseline status/log/summary, upload receipt, post-update status, screenshots,
`timing-menu.log`, `timing-bloodgulch.log` and the follow-up gameplay log.
