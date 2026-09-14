# September 14 rendering and math candidates on hardware

The new candidates produce small, scene-dependent changes. They have not
achieved stable 20 or 30 FPS. Early visibility completion helped the tested
base view most consistently. Texture binding reuse remains disabled by default.

## Method

The updater installed and boot-confirmed runtime
`8b89a41eabd3f326413166dffade4a50875398aa7d02c6a0e103d346e224a778`
over Wi-Fi. Normal multiplayer menus loaded solo Blood Gulch.

Each candidate ran three off/on/off trials in each of two stationary views,
with 60 settling and 120 measured frames per arm. Every camera check passed.
Results pool the exact elapsed microseconds: 720 off and 360 on frames per
candidate/view. Screenshots and bulk log downloads occurred outside measurement.
Live simulation and the same status polling continued in every arm.

Settings stayed at 640 × 360, texture maximum 128, low model detail, triple
buffering, vertex worker and native object basis enabled, native palette and
bounds disabled, phase timing off, and a 30 FPS cap. The requested 500 MHz
CPU clock fell back to 444 MHz; bus/GPU/crossbar were 222/222/166 MHz.
All other native math and rendering policies remained fixed within each test.

## Results

Positive saved time means the enabled arm was faster.

| View | Candidate | Off FPS | On FPS | Saved ms/frame |
| --- | --- | ---: | ---: | ---: |
| Base | Early visibility completion | 16.150 | 16.531 | 1.429 |
| Base | Native point transform | 16.079 | 16.115 | 0.139 |
| Base | Texture binding reuse | 16.003 | 16.103 | 0.389 |
| Valley | Early visibility completion | 12.145 | 12.198 | 0.357 |
| Valley | Native point transform | 12.055 | 12.185 | 0.886 |
| Valley | Texture binding reuse | 12.213 | 12.084 | -0.874 |

Early visibility saved 0.974–1.743 ms in all three base trials, but one valley
trial was slower. Point transforms and texture binding reuse had mixed trial
directions in both views. These small averages do not establish a broad FPS
improvement. Neither view represents campaign performance. The matrix and
quaternion compiler changes were not isolated by these comparisons.

The texture cache's emulator reduction in API calls is real, but it does not
establish a hardware frame-time benefit. It stays optional. Early visibility
also remains opt-in while broader correctness and workload checks continue.
The next diagnostic separates the scene's visibility routines, draw callbacks
and object preparation; its instrumented timings must not be presented as
normal-build performance.

## Evidence and related work

Private evidence is under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z`:
`physical-early-visibility`, `physical-point-math`, `physical-texture-state`,
their `physical-valley-*` counterparts, and
`physical-lunch-comparison-analysis.json`. Full logs, screenshots, owned game
source, binaries and pairing credentials remain outside Git.

See the [earlier hardware baseline](hardware-20260914-rendering.md),
[native point transform validation](native-point-transform-20260914.md), and
[texture cache implementation](texture-state-cache-20260914.md).
