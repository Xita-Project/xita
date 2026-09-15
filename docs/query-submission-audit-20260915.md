# Visibility-query submission audit — September 15

The frequent query path contains both translated math and repeated native draw
preparation. A projection-only replacement was tested and withdrawn. The next
candidate should address that repeated preparation, preserving each query's
identity, coverage and submission order.

## Actual submission path

After a rectangle survives projection and clipping, Halo's `63C00` begins a
visibility query, begins a quad, submits four positions through separate
`SetVertexData4f` calls, ends the primitive and ends the query. Each position
submission snapshots all 16 float4 attributes: 1,024 bytes across four vertices.
The immediate flare bridge then packs four 40-byte vertices into memory retained
until the frame slot retires.

This path uses the particle vertex declaration and the main 3D command list.
Recording captures indices, shader state, constants and textures. The command
retains its visibility slot, and replay enables that query around the ordered
draw. Combining different query IDs into one ordinary draw would lose their
individual results. The legacy UI quad queue is not involved in this path.

A single cryo-room histogram after a camera turn recorded 442 draw commands.
Of these, 119 used vertex program 56 with pixel-state hash `CA23BB16`, and 104
used the same vertex program with `C2B5AC38`. Their association with query and
visible-flare work was initially inferred from setup; the histogram now records
the captured query slot and color mask to check that association directly.
The earlier 292 `63C00` entries/frame came from a different view and includes
rejected rectangles. Neither count is a hardware timing measurement.

The updated diagnostic confirms that association in a new 467-command cryo
capture: exactly 119 recorded and replayed queries use `CA23BB16`, with color
mask zero. All 119 resolve to a depth-only fragment with **no texture samplers**,
after recording prepared texture stage 0. The 104 `C2B5AC38` draws are visible
flare draws with no active query and RGB writes enabled. The camera was turned
normally after entering the campaign; these totals are not a controlled frame-time
comparison with the earlier capture.

## Projection experiment rejected

A private prototype replaced projection routine `637A0`, with complete-image
and function-signature guards. It retained the original point transform call,
memory access order, float spills and guest context. Host comparisons covered
12,288 enabled/disabled cases, including aliases, split guest pages, rounding
modes, exceptional values and state changes at the call boundary. Those passed,
but comparisons of the actual Vita-linked ARM code found differences.

| Prototype | Modeled ARM instructions, 2,560 cases | Correctness result |
| --- | ---: | --- |
| Local state across the whole routine | +2.79% | Exceptional-value context mismatch |
| Local state only after early rejection | −2.42% | Exceptional-value guest-memory mismatch |

The first mismatch included a NaN payload in an inactive x87 register. The second
changed four guest-memory bytes; its cause has not been established. The checks
were not weakened to accept either difference. These instruction counts are not
hardware cycles or FPS. Neither prototype was booted in the game emulator or
installed on hardware. Both hook and helper were removed from the gameplay build.
The private source, failed comparisons and artifacts are preserved.

## Next boundary

Distinguish rectangle construction from native submission before choosing the
next replacement. Repeated full draw-state synchronization and texture preparation
are candidates. Replay already substitutes a constant fragment program when a
successfully linked color-masked shader cannot discard fragments or replace
depth. That does not establish that recording can safely omit textures: shader
variants, fallback links and coverage-changing programs must remain valid.

The confirmed query draws suggest an earlier depth-only recording path. A proof
published after successful shader linking could let subsequent eligible draws
omit texture preparation and select the already linked constant fragment. It
must be tied to the vertex layout, canonical pixel program and alpha mode, with
explicit publication between the render and recording threads. Unknown variants,
texture-dependent clipping, depth-writing shaders, overrides and failed links
must retain the original path. This optimization is not implemented yet.

The on-demand query/color-mask diagnostic changes no ordinary draw behavior.
The ARM comparison tool now includes `637A0` fixtures to retain the evidence that
caught the failed experiment. Generated game code remains excluded from source
exports. Physical Wi-Fi status still times out; the preserved hardware slots have
not been changed. Standard-settings hardware comparisons, driving, campaign
combat and resolution of the reported GPU crash remain outstanding.

The final diagnostic builds with VitaSDK and passes package-contract verification
for all 1,588 entries. The restored projection routine matches the preceding
linked ARM implementation in 5,120 finite/exceptional-value comparisons, with
identical guest memory, context, FP status and modeled instruction counts. The
exact diagnostic runtime boots and enters campaign in the private emulator:
`9229863f55b924a79ecab51e2418946e4a8a081c7edc36f795189507ad9cc749`.
The histogram's depth-off vertex preview now also respects the actual vertex
count and immediate stride, instead of reading six vertices from a four-vertex
quad. This affects diagnostics only.

Private evidence is under `engine-restructure-20260914T2300Z`: the
`query-projection-experiment/` source archive, `query-projection-*-arm-*`
comparisons, host logs and `query-audit-hist-summary.json`.
The final linked comparisons and replay capture are recorded in
`query-audit-evidence.json` and `query-audit-replay-summary.json`.
