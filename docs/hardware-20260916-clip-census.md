# Clipping comparison and worker lighting census

The clipping region has no consistent physical benefit in the tested Blood
Gulch view and remains OFF. The lighting census finds that the observed calls
already execute in object workers; an owner-only query gather would miss them.
Stable 20 FPS and representative driving/campaign crash stability remain unmet.

## Installed candidate and method

Runtime `71ab31a4f6de613a8d5611f179efd67e1d4aabac4bbf7b3b0fe9daa27a4c4673`
was verified and boot-confirmed in slot B through the authorized Wi-Fi updater.
It builds source `c8f5dd0` with optional clipping and census support, both OFF at
startup, and retains the measured background-report policy. The package has
1,588 unchanged member names and the same launcher/shader/asset contract; only
the game executable and boot digest differ. The preserved slot A is unchanged.
The subsequent GPU packet timing diagnostic is not present in this executable.

Normal menus loaded solo Blood Gulch, facing the blue base. Saved standard
graphics stayed fixed: 960×544, texture maximum 256, original glow/particle/
material quality, triple buffering, both object workers and no frame cap.
Effective CPU/bus/GPU/xbar clocks were 444/222/222/166 MHz; the saved 500 MHz CPU
request is unsupported. Different earlier views are not cross-build baselines.

Each clipping trial uses 60 settling and 120 measured frames per OFF/ON/OFF arm.
All three camera checks pass. Screenshots and bulk log downloads are outside
measurement, with identical status polling in each arm. Live simulation
continues. Every arm includes the same ARM clip operand-order correction and
compiled-in, runtime-OFF census hooks.

| Trial | OFF before FPS | ON FPS | OFF after FPS | Saved ms/frame |
| --- | ---: | ---: | ---: | ---: |
| 1 | 10.862 | 10.604 | 10.898 | -2.391 |
| 2 | 10.906 | 10.906 | 10.839 | 0.285 |
| 3 | 10.847 | 10.807 | 10.843 | -0.322 |

Pooling exact elapsed times gives 10.8657 FPS OFF and 10.7709 ON, or 0.8093 ms
slower per enabled frame. This short sample has mixed trial directions and
does not establish the hardware cause. It provides no reason to enable fusion.
Full ON report windows confirm 1,200 regions and 6,240 fused clips per 60 frames,
matching the common clip count, with no capacity failures. The path genuinely
ran. The `input-vertices` diagnostic counts positive count loads; an in-place
copy can load/count twice, so it is not a unique-vertex or vertices-per-clip
measurement. The earlier 16.40% modeled instruction reduction used a different
fixture distribution and build configuration, not this physical workload.

## Lighting workload already on workers

One completed census37 OFF/ON/OFF trial measures 10.898/10.902/10.879 FPS with
matching views and successful restoration. This is an observer-cost test,
not an optimization. The ON 120-frame window records:

- 17,212 worker entry hooks;
- 12,578 worker query hooks and 12,578 worker removal hooks;
- Zero admitted owner groups, owner query metadata reads or owner orphans.

These hooks count different events and cannot be added as independent queries.
Worker admission deliberately reads no guest metadata, so membership, actual
traversals, query size, aliases and safe parallel eligibility remain unknown.
Zero owner candidates does not mean absent lighting work. It means the proposed
owner-only gather does not cover this observed production boundary. Investigate
the existing worker shared-geometry critical section and ordered publication
before adding another query dispatch layer. No new query threads are enabled.

## Startup logger handoff and limits

The preceding startup-ON runtime `575ecdbc…` completed an update exit. Its preserved
log contains recording pause, GPU drain completion, display detach/checked
logger drain and boot-helper handoff in order; the preserved runtime then
booted successfully. The checked shutdown path retries failed logger drains
before permitting this handoff. Final accepted/written/synced counters were not
sampled after shutdown, so this is not a power-loss durability claim. The older
preserved runtime lacks historical-log HTTP routes; the final tail was retrieved
as history slot 2 after booting the new candidate.

All comparisons restored their optional modes to OFF. A charged plasma shot
afterward reduced energy from 100 to 89 and the game continued. These short stationary
tests do not establish driving/campaign stability or resolution of rare crashes.
Native clipping stack headroom also remains unmeasured. The unchanged saved
graphics and successful single shot must not be described as stable 20 FPS.

Private evidence: `engine-restructure-20260914T2300Z/physical-clip-census`,
including installed/confirmed receipts, `startup-on-handoff.json`,
`clip-compare/analysis.json`, `light-census/analysis.json`, original logs and
screenshots. Package/ELF and owned generated inputs remain outside Git.

Next work separates GPU notification observation from submission time and
locates any existing scene completion after the last visibility-query writer.
In an earlier tank-side view, exact flare-result waits averaged about 19.23 ms
per frame across seven report windows. Native results currently publish at the
final fragment fence. That is a real dependency wait, not a measured recoverable
frame-time saving or proof that an extra scene split would improve throughput.
