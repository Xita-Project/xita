# Model LOD and material preparation candidate

The first implementation following the [Wii archive review](wii-reference-20260909.md)
adds earlier selection of authored model LODs and reuse of unchanged shader
colors. This is new Xita code checked against the supported Xbox 3925 executable
and owned maps. No Wii source or assets were imported; the archive's provenance
and license remain unconfirmed.

This candidate is built and [installed over USB](hardware-20260909-model-prep.md).
The published `xita-gameplay-20260909.vpk` does not contain these changes.
No physical Vita FPS gain has been measured.

## Model detail

Graphics in the dashboard and in-game panel gains **Model detail**. The default
**Original** leaves tag memory unchanged. **Balanced** and **Low** raise the
four existing mesh-transition cutoffs by 1.25 and 5/3 respectively, selecting
simpler meshes sooner. The fifth cutoff, governing minimum visibility, stays
unchanged. The option requires closing and relaunching Xita.

`XV_MODEL_DETAIL=2` selects Original, `1` Balanced and `0` Low. Invalid values
fall back to Original. Changes apply to freshly loaded guest tag memory; map
files, saves, collision data, geometry and index buffers are untouched.

Only eligible character/scenery models with real alternate meshes and valid
cutoff/region/permutation tables are adjusted. A model shared with a weapon,
vehicle or cinematic object retains its original thresholds. Models under
weapon/vehicle paths are excluded even when referenced by scenery. First-person
weapon meshes and BSP geometry retain their original behavior. Malformed
metadata is skipped instead of selecting an invented fallback mesh.

| Map | Distinct models adjusted at Balanced or Low |
| --- | --- |
| Blood Gulch | 1: cyborg |
| Battle Creek | 1: cyborg |
| Pillar of Autumn (`a10`) | 4: marine, cyborg, elite, grunt |
| Halo (`a30`) | 7: crashed lifepod, two marine models, cyborg, grunt, elite, jackal |
| Main menu (`ui`) | 0 |

Most inspected rocks and trees have no usable alternate LODs. This option is
unlikely to help much in an empty Blood Gulch match. Campaign scenes with
several characters are the useful comparison. Lower LODs do not necessarily
reduce draw-call count; any vertex/preparation benefit needs measurement.

The original Xbox selector at `0xA27F2–0xA281B` reads ascending compiled-cache
cutoffs at `mode + 8`. This order was verified in the executable; editor-format
field names alone are insufficient. The runtime logs `[model-lod]` once for each
fresh map tag load when a reduced detail level is enabled.

## Shader color reuse

`xd3d_ps_sync` previously expanded all 18 packed stage colors into 72 float
components on every dirty shader update. It now expands only changed colors,
using exact 32-bit comparisons. Shader identity, animated colors and final
combiner constants still update normally. First synchronization and state
resets force a complete expansion.

The existing diagnostic `XV_PREP_STATE_CACHE=0` disables both shader identity
and color reuse for comparisons. `[material-prep]` reports the numbers of reused
and expanded colors alongside the periodic preparation report. This measures
avoided conversions, not elapsed time or FPS.

The production regression sequence retains identical shader identities and
float colors across 18,000 updates, mutable uploads, resets and split guest
pages. With caching on it reuses 210,035 colors and expands 114,091; with caching
off it expands all 324,126. These are test counts, not hardware gameplay results.

## Validation

- Dashboard and overlay navigation, persistence and relaunch behavior pass
  address/undefined-behavior sanitizer tests.
- `tools/test_model_lod.py` passes default/fallback checks and 19 malformed or
  protected-model guards, including shared weapon/vehicle owners.
- Optional `--xbe` checks execute the actual Xbox selector in Unicorn across
  transition boundaries, confirming lower/equal selection and retained extrema.
- Owned `bloodgulch`, `beavercreek`, `a10`, `a30` and `ui` map checks confirm that
  changed bytes are confined to eligible transition fields. Existing material,
  glow, particle and decal quality tests still pass.
- Native build, CPU preparation tests, NV2A method checks and shader identity
  checks pass. Physical testing remains pending.
- An isolated Vita3K run displays the new dashboard and overlay row, loads
  `a10` with four models adjusted, renders the cryo room and technician, and
  responds to camera input. Selected 60-frame cryo-room windows reuse about
  90% of color expansions. The emulator is capped at 20 FPS; this does not
  measure Vita throughput or establish an FPS improvement.

## Next measurements and preparation work

Compare Original and Low in the same campaign encounter with the same
resolution, texture settings and clock. Record FPS/frame timing, draw counts
and `[material-prep]` counters. Do not compare unrelated runs or sum nested
asynchronous timing categories.

Visibility metadata reuse and offline geometry/texture caches remain follow-up
work. They require cache invalidation, map/settings fingerprints and preserved
draw ownership. Broad draw reordering or running shared guest AI state on a
second core is not part of this change.

Installation also needs investigation: the September 9 gameplay VPK contains
1,585 files, including 1,574 individual GXP programs and 1,581 files smaller than
4 KiB. Its CRC check passes, but a hardware report describes 1 KB/s at 98–99%.
Small-file overhead is a hypothesis, not a confirmed cause. A single validated
shader bundle could reduce file creation and open calls; it needs loader and
packaging work rather than simply putting another ZIP inside the VPK.
