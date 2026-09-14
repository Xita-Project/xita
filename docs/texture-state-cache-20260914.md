# September 14: scoped texture binding cache

A new, optional mesh submission cache removes repeated
`sceGxmSetFragmentTexture` calls when the complete resolved descriptor already
matches the active sampler unit. It defaults **off** while physical Vita
frame-time benefit remains unmeasured.

A count-only Vita3K probe preserved every API call. One stationary Blood Gulch
view requested 331 bindings per frame, of which 166 were identical to the
preceding binding on that unit. Movement and firing windows also showed about
50% repeated bindings. These are binding counts, not draw calls or GPU cache
miss measurements. The main menu had much less reuse.

## Scope and correctness

The key contains all 16 bytes of `SceGxmTexture`, including data address,
layout, dimensions, format, palette and sampler settings. Cache entries own
a copy; neither the source descriptor's pointer nor the texture handle alone
is sufficient. The linked fragment program's actual sampler index determines
the entry, including remapped units above the original four Xbox stages.

The cache belongs to the GXM pump and lasts for one uninterrupted mesh replay
range. Every clear invalidates it. UI replay, render-target changes and new
scenes begin new ranges. UI keeps its existing binding path. Draw order,
vertex/index data and fragment programs are unchanged.

Texture resolution still happens first: previous-frame substitution,
cube-face conversion and neutral fallback all precede the comparison.
Active render-target feedback is rejected even if its descriptor would match
a cached entry. A failed binding invalidates that unit and is retried on the
next request. Out-of-range units reach the original API without indexing the
cache. The existing draw policy after API errors is preserved.

The [VitaSDK GXM interface](https://docs.vitasdk.org/group__SceGxmUser.html)
describes fragment texture binding as active state for subsequent calls and
provides 16 sampler units. The corresponding
[Vita3K implementation](https://github.com/Vita3K/Vita3K/blob/master/vita3k/modules/SceGxm/SceGxm.cpp)
was inspected to check descriptor copying and fragment-program changes. This
supports the implementation boundary; it does not predict hardware speed.

## Validation

- The production cache passed 120,000 deterministic draw-state comparisons
  under ASan/UBSan. All observed bindings matched the uncached path, with
  48,374 redundant calls removed across 1,898 invalidation boundaries.
- Every one of the SDK descriptor's 128 bits is checked for invalidation.
  Tests cover copied descriptor storage, failed replacements, disabled mode,
  sampler 15 and out-of-range units.
- Production texture-resolution tests cover active feedback, previous-frame
  substitution, sampler remapping, temporary cube-face descriptors and
  partially bound draws that are rejected at a later sampler.
- Frame acquisition, render-target ordering, benchmark restoration, profile
  counters and authenticated HTTP selection pass their existing host checks.
- The native package changes only `game-a.self` and `boot-game.txt` relative
  to the validated point-transform package. Its updater handoff was confirmed
  in Vita3K, with the expected executable hash and inactive-slot switch.

The functional emulator comparison measured **19.960 / 19.963 / 19.955 FPS**
(off/on/off), with a stable camera and the 20 FPS cap retained. A separate pass
with render profiling enabled measured **19.968 / 19.968 / 19.969 FPS**. Nearby
whole profile windows in that second view show about 290 mesh binding requests
per frame: 290 API calls when off and 152 when on, with 138 skipped and zero
binding errors. The ordinary draw count remains 145 per frame. Boundary windows
mix settings and are excluded from those counts.

Normal menu navigation, walking, turning and plasma-pistol firing were also
captured with the cache enabled. The comparison restored the enabled emulator
configuration afterward; separate tests cover the shipped default-off mode.
These checks establish exercised behavior, not a physical Vita speedup.

## Hardware comparison

Use the same stationary first-person view and ordinary graphics settings:

```sh
python3 tools/vita_remote.py --config /private/path/remote-client.json \
  benchmark /private/path/texture-state-001 --kind texture-state --runs 3
```

The comparison runs off/on/off at the current resolution, with 60 settling
and 120 measured frames per phase. It changes only the texture binding cache
at a drained boundary and restores the configured default on completion,
cancellation or loss of the first-person view.

`XV_TEXTURE_STATE_CACHE=1` enables the experiment outside a comparison;
omitting it keeps the cache off. `[texture-bind]` reports mesh requests,
actual API calls, identical calls skipped and API errors in each 60-frame
render-profile window. These counts exclude the separate UI binding path.
Retain normal `[render-draws]` and frame-time reports to distinguish fewer
bindings from fewer draws or faster rendering.
