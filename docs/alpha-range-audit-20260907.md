# Remaining material alpha tests — September 7, 2026

The sampled draws do **not** support extending the whole-texture opacity proof
to a broader alpha interval. All remaining alpha-tested `154066FD` material
draws in three Blood Gulch captures reference uploads spanning **0 through 255**.
The cryo capture has no remaining alpha-tested draws in that family. Keep the
current opacity optimization; do not add range scanning to normal builds on the
assumption that it removes more alpha tests.

## Observation method

An independent native observation build derives from the preserved installed
vertex-comparison source. It records conservative minimum/maximum alpha when a
new BC or decoded RGBA texture version is uploaded, after the selected mip and
coverage transformations. All uploaded mip levels contribute. BC partial blocks
include their unused texels conservatively. BC3 bounds use floor/ceil around
rational interpolation, covering integer rounding and fractional decoding.
Cubes, foreign descriptors and render-target aliases do not provide a proof.

An explicitly requested histogram frame logs the cached upload bounds alongside
the material draw's index count, captured alpha predicate, sampler addressing and
existing opaque-shader selection. The audit never changes shader selection,
textures, vertex/index contents, submission order or queue policy. It adds upload
metadata work in this private build; its timings are not an optimization result.
The range helper and test live in root, but the runtime instrumentation remains
in the isolated audit stage, outside the installed and other candidate builds.

| Captured view | Material draws | Existing opaque proofs | Remaining alpha-tested draws | Their indices | Extra interval candidates |
| --- | ---: | ---: | ---: | ---: | ---: |
| Blood Gulch, facing base, frame 1792 | 86 | 56 | 28 | 4,428 | 0 |
| Blood Gulch, turned, frame 2192 | 79 | 57 | 20 | 3,492 | 0 |
| Blood Gulch, after walking, frame 3232 | 72 | 51 | 20 | 3,492 | 0 |
| Campaign cryo bay, frame 5696 | 32 | 32 | 0 | 0 | 0 |

Material totals also include draws whose alpha test was already disabled or
ALWAYS. Every remaining captured test is enabled `GREATER`, reference 127
(`atest=1047F`). Four Blood Gulch texture headers account for the remaining
material draws: `004AB834`, `004AB874`, `004ABA14`, `004ABA34`. Each upload's bounds
are `[0,255]`. Such a range crosses the cutoff. It cannot establish that every
fragment of a particular draw passes; it also does not establish which texels a
particular triangle actually samples. No per-triangle coverage claim is made.

These are four emulator frames at the captured positions, not the physical
Vita's earlier 26-draw view and not a representative average. Candidate counts
and indices are work observations, not GPU-cost estimates or FPS improvements.
`tools/analyze_alpha_ranges.py LOG --output REPORT.json` preserves all parsed
records and reproduces the table. Its interval screen includes a full-byte
margin at the cutoff; no borderline arithmetic can create a positive result in
these full-range observations.

## Validation and archive

`make -C recomp/host test-alpha-range` and ASan/UBSan pass 644,288 BC cases,
including every BC3 endpoint/selector combination, independent rational and
integer-decoder bounds, multiple blocks, mip tails and RGBA row padding.
The actual staged texture cache/worker tests pass. Native build and private
menu/Blood Gulch/campaign rendering checks pass. Four requested ownership
captures find no changed data before GPU completion. The archived run analysis
checks fence errors, upload failures, draw-storage drops and normal Finish calls.
The private emulator is stopped and its previous executable/configuration
restored. No performance gain or hardware crash resolution is asserted.

Archive:
`/home/birchwoodgod/xita-backups/2026-09-07-211922-alpha-range-audit/`.
Its `build-stage` contains the preserved observation source and native products.
Native SELF: 34,796,382 bytes, SHA-256
`e5c5f5ae3d4c285a49119f2d3efde4e72afe6d44c951724b1c8541967c518f95`.
The archive retains the source manifest/patch, tests, screenshots, parsed
observations, raw logs and launch/cleanup records. The candidate was not installed.

A read-only USB check at the beginning of this work found the expected installed
executable (`4663b025…`) and unchanged saved configuration (`8faaa4d…`). The device
log was still last modified at 18:39:36, with the old residency result and no
`vertex-compare` result. USB was safely unmounted. This does not require another
residency test; the pending installed comparison remains scalar/NEON/scalar.

## Next action

Prepare an isolated comparison of the existing dedicated `GREATER` cutout
shader. Its six source variants preserve all other shader arithmetic and the
original rejection behavior; the staged source/embedded-byte preflight passes.
The compiled files are 26–33% smaller than their generic alpha-test versions,
which is **not** a GPU-time or FPS prediction. This path retains the actual
alpha cutoff and targets the remaining Blood Gulch draws directly.

The dedicated shader already exists locally but remains disabled in the installed
source. It still needs a native application/emulator comparison and physical
measurement. Keep that test separate from vertex comparison, deferred-flare
scheduling and RGBA swizzling. Standard graphics settings, the 20 FPS hardware
goal and the unverified driving crash remain unchanged.
