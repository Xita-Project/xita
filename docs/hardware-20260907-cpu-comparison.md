# Controlled CPU comparison — September 7

The three recent CPU optimizations show **no measurable FPS benefit in this
Blood Gulch view**. The verified off/on/off result at fixed 544p is
**7.079 / 7.065 / 7.204 FPS**. All three paths switch correctly, camera and
supporting draw counts match, and configured defaults return after completion.
This closes the missing CPU-comparison measurement; it does not meet the
sustained 20 FPS target or establish that these changes improved performance.

## Collection and conditions

Read-only USB archive:
`/home/birchwoodgod/xita-backups/2026-09-07-130508-unused-texture-hardware`.
Collection completed at 13:05 CDT with 63 copied and hash-verified files,
487,165,054 bytes. The installed executable is 32,918,474 bytes, SHA-256
`d97bffe770677b02c7436edb48f89ceabf1d8302ee239e81ab9be384d3b23b3e`,
matching the [validated build](unused-texture-preparation-20260907.md).

The configuration still matches the predeployment backup byte for byte:
544p, textures 256, automatic filtering, mip smoothing On, High material/glow/
particles, CPU 444 MHz and a 20 FPS cap. The test uses **L + R + Square** and
holds resolution/effects/cap fixed. No device files changed; USB is safely
unmounted. No September 7 screenshots were present.

The three toggled changes are clipper integer locals, palette hash reuse and
skipping unused texture-stage preparation. Other CPU, worker, shader and wait
changes remain enabled throughout; this test does not evaluate their benefit.

## Exact phase result

Each phase settles for 60 frames, then measures 120 frames.

| Phase | FPS | Mean frame time |
| --- | ---: | ---: |
| Off before | 7.079 | 141.268 ms |
| On | 7.065 | 141.541 ms |
| Off after | 7.204 | 138.811 ms |

Pooled off performance is **7.141 FPS / 140.039 ms**. On is 1.501 ms slower
than the pooled off phases, a throughput difference of **−1.06%**. The two off
phases themselves differ by 2.457 ms / **1.77%**. A single short comparison
does not establish a repeatable regression or statistical equivalence; there
is no demonstrated FPS gain here. Avoid describing the work-count reduction
as an equal reduction in CPU time or frame time.

Position `104.8391 -157.1819 0.8523` and forward
`-0.62677 0.77313 0.09735` match throughout, with `view-ok 1` in every phase.
All supporting measured reports have 229 draws/frame. Simulation continues
running, so equal camera and draw totals do not freeze all workload details.
Earlier resolution runs used different views and cannot establish build gains.

## Switch verification and supporting timing

Each phase contains two 60-frame reports. Their boundaries differ from the
exact 120-frame FPS interval, although the preceding settling interval ensures
their work counters use that phase's selected paths.

| Work per 60-frame report | Off before | On | Off after |
| --- | ---: | ---: | ---: |
| Native clip calls | 37,500 | 37,500 | 37,500 |
| Integer-local clip calls | 0 | 37,500 | 0 |
| Palette hashes reused / calculated | 0 / 1,200 | 1,200 / 0 | 0 / 1,200 |
| Texture stages prepared | 54,300 | 39,660 | 54,300 |
| Unused texture stages skipped | 0 | 14,640 | 0 |

On skips **27.0%** of bound texture-stage preparations in this view. The
completion log restores 544p; later reports show integer locals, palette reuse
and skipped stages active again under the configured defaults.

| Supporting elapsed metric | Off before | On | Off after |
| --- | ---: | ---: | ---: |
| Draw preparation | 11.74 ms | 10.66 ms | 10.02 ms |
| Render submission | 6.22 ms | 6.17 ms | 6.14 ms |
| Final graphics-completion wait | 69.54 ms | 69.65 ms | 69.78 ms |
| Query waiting per guest frame | 33.30 ms | 34.11 ms | 33.28 ms |
| Query completion-to-resume | 89 µs | 93 µs | 94 µs |
| Nearby C0 / C1 / C2 median busy | 3 / 8 / 72.5% | 3 / 11 / 71% | 3 / 9 / 72% |

Draw preparation decreases across the run, including the final off phase;
comparing only off-before against on would overstate its benefit. Render and
query waits overlap engine execution and other reported intervals. Final
Finish is elapsed CPU-side waiting, not exclusive GPU execution time. It is
not valid to add these rows or infer that removing the 70 ms wait would make
the remaining frame CPU time equal to 70 ms.

No texture decodes occur during measured phases. All 32 complete render
reports have matching stage sums and frame counts, 60 final Finish/display
queue calls and zero intermediate target Finish calls. No explicit current
shader/render-target failure or work-table overflow is found. Historical
shader errors in `xboxvita*` logs are unchanged since the predeployment backup.
Geometry diagnostics were off; this test does not certify visual correctness.

## Next development step

**No additional user CPU benchmark is needed now.** Keep the installed build
and saved standard settings. Do not count this bundle as a demonstrated
speedup, and do not extrapolate its result to all earlier CPU/threading work.

Shift the next investigation to the common rendering passes behind the long
graphics waits. The earlier resolution tests also show material savings when
pixel count drops. Identify the relevant render target, compiled program,
alpha/depth/blend state and repeated geometry before choosing a shader or
pass optimization; the current top-eight index counts alone cannot rank GPU
cost. Preserve rendering behavior and standard settings in the eventual
candidate, then validate it locally before another hardware comparison.

Source inspection maps frequently submitted canonical keys to these programs:

| Profile key | Program family | Source computation |
| --- | --- | --- |
| `154066FD` | `ps_154066FD_*` | Eight combiners, material/detail/cube inputs |
| `A01D09CF` | `ps_DEB42ED7_3D*` | Two combiners, 2D textures and analytic normalization |
| `8ED40330` | `ps_D7B21DB5_0C*` | Two combiners and texture-derived alpha |
| `A16C769C` | `ps_793F7B11_3C*` | Four-texture, three-combiner modulation |

`A01D09CF` and `8ED40330` have equal submitted draw/index totals in this view.
That is a candidate for examining repeated passes, not proof that either
pass is redundant. These are investigation targets, not measured GPU cost
rankings or permission to drop a rendering effect.

Archive contents include raw files and hashes, collection metadata, general
analysis, CPU phase analysis, path-switch verification, log inventory and
their reproducible scripts. No game code or executable changed in this
collection. Future test reports must state the result, next development step
and whether another user test is needed.
