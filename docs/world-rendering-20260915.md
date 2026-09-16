# World rendering and effects — September 15

The user reports about 30 FPS when looking at the sky and in the main menu,
with drops around campaign effects and dense geometry. Current physical logs
support a large view-dependent cost, but do not isolate polygon count from
pixel shading, draw preparation, visibility work or object simulation.

## Captured physical views

Runtime `3753961f842872504f3922d459d6a5d2b27f56708f8b68e9df5bb3e88895c051`
retains the standard native-resolution settings and both experimental workers.
Its initial Blood Gulch capture includes:

| 60-frame window | Lighter, upward-facing view | World-facing view |
| --- | ---: | ---: |
| Logged FPS | 26.8 | 16.4 |
| Draws/frame | 55 | 131 |
| Draw HLE | 3.0 ms/frame | 8.3 ms/frame |
| Object batch wall time | 11.45 ms/frame | 17.53 ms/frame |
| GPU completion notification latency | 21.64 ms/frame | 53.84 ms/frame |
| Texture decodes | 0 | 0 |

These are different positions during a live walk, not a controlled same-view
optimization benchmark. The timing scopes overlap and must not be added.
A subsequent campaign sample records 259 draws/frame at 8.6 FPS, with 18.6 ms
in draw HLE. Its preparation subscopes include 6.74 ms in streams, 3.32 ms in
indices, 2.53 ms in textures and 1.90 ms in state processing. The object batch
wall time is 36.09 ms/frame. The exact visibility wait reports zero in that
window. These numbers also overlap; they do not establish an exclusively GPU
bottleneck. Vertex stream preparation is a concrete CPU target alongside the
world-material experiment.

Completion latency includes submission/scheduling/polling delay; it is not a
measurement of exclusive GPU execution. The upward view's forward Z is 0.64;
the world view's is 0.13. The main menu is a lighter workload, not proof of
30 FPS campaign performance. Effect-specific attribution still requires capture
of the actual firing or explosion interval.

## Opt-in replacement-blend candidate

Some draws with blending disabled still link an ADD / ONE / ZERO blend program
because they preserve selected destination channels, commonly alpha. The
candidate links the same translated fragment shader with blend functions NONE,
retaining the exact channel mask. No shader arithmetic, alpha test, depth state,
textures, vertex data or draw order is changed. Other blend equations and factor
pairs are ineligible. The driver might already simplify identity blending, so
no speedup is assumed.

A private emulator frame has 178 mesh draws, including 40 disabled-blend draws
with a partial channel mask. Those draws submit 10,269 indices across five vertex
programs; 26 use the cutout material combiner. This is evidence of applicability,
not evidence of GPU cost or an estimate of unique polygons.

`XV_BLEND_REPLACE` defaults to zero. The remote `blend-replace` comparison holds
resolution constant and uses off/on/off phases with 60 settling and 120 measured
frames each. Policy changes occur after the existing submission drain. Baseline
and candidate links have separate cache keys and survive toggles; failed
candidate links fall back to the baseline shader with the same alpha mode.
The extra links consume the existing shader/link-cache budget, so cache pressure
must be checked before making this a default. No program is freed mid-frame.

## Validation and next work

Host tests cover all color masks and alpha modes, configured off/on/unset,
repeated toggles, restore, failed candidate links, existing baseline lookup in a
full cache and ineligible equations. ASan/UBSan passes. Benchmark tests cover
completion, cancellation, loss of gameplay view, absent implementation and the
actual drained dispatcher. Remote client/server selector tests pass.

Native compilation and package verification pass. The build retains the
voice-stop crash fix, and only the runtime and digest differ from that package.
Vita3K completes off/on/off with the same view and 13 candidate shader links.
The capped emulator reports 19.942 / 19.958 / 19.967 FPS; these values do not
measure Vita performance. A second run captures the enabled rendering on owned
DISPLAY112 and verifies restoration. The enabled world/HUD/weapon view shows
no obvious change in this scene. The subsequent physical comparison below finds
no useful performance gain, so the candidate remains disabled. Next, separate
effect startup costs from sustained transparent overdraw and audit expensive
world passes.

## Physical blend comparison

Runtime `37f761dc071a7489f8dcd176bcbbe6e4152d4a0f03d3a9dbff2e13e842c28627`
completed three native-resolution off/on/off trials at the same stationary
Blood Gulch view, with standard graphics and the existing workers retained:

| Trial | Off before | On | Off after |
| --- | ---: | ---: | ---: |
| 1 | 10.732 FPS | 10.714 FPS | 10.712 FPS |
| 2 | 10.717 FPS | 10.718 FPS | 10.652 FPS |
| 3 | 10.625 FPS | 10.670 FPS | 10.655 FPS |

Eight candidate links were created and all trials passed the camera check and
restored the configured policy. Relative to the mean of each trial's off arms,
the differences are -0.075%, +0.314% and +0.282%. This does not establish a useful
performance gain. Leave replacement blending disabled. No STOP was logged in
the captured run; this stationary comparison does not validate crash-free combat.

The view contains about 130 draws/frame. Representative preparation scopes are
1.52 ms/frame for indices and 3.05 ms for streams; stream uploads retain about
327 KiB/frame and compare about 329 KiB. These caller timings overlap other
pipeline work. Next, measure the existing exact index-reuse candidate while
preserving the immutable vertex snapshots that protect against geometry spikes.

Physical evidence: `world-followup-20260916T002003Z/blend-comparison/` under the
engine-restructure validation directory. Its UTC date is September 16; local
testing occurred on September 15.

Private evidence: `physical-object-voice-stop/`,
`replace-blend-hist-summary.json`, `replace-blend-eligible-draws.json`,
`replace-blend-checks.json` under the engine-restructure validation directory.
