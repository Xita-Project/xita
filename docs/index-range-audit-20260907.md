# Vertex index-range audit — September 7, 2026

Full index minima confirm some unnecessary leading vertex data, but the amount
varies by view. Blanket index rebasing is a lower-priority experiment after this
audit. It has not been implemented or measured as a performance optimization.

The current recorder retains zero through the highest referenced vertex and
passes raw stream bytes to immutable GPU uploads. The probe reads every retained
index only during an explicitly requested histogram frame and logs minimum,
maximum, primitive, base vertex and immediate status. Existing stream logs supply
each requested stride and length. Draws, indices, vertices, shaders and queue
policy are unchanged. This is a native build running privately in Vita3K, based
on the preserved installed vertex-comparison source; the deferred-flare candidate
is separate.

| Captured view | Draws | Draws whose minimum is zero | Requested stream bytes | Unreferenced leading bytes |
| --- | ---: | ---: | ---: | ---: |
| Blood Gulch, facing base | 134 | 120 | 764,152 | 175,656 (22.99%) |
| Blood Gulch, turned toward valley/rock | 149 | 125 | 852,040 | 26,888 (3.16%) |
| Campaign cryo bay after opening skip | 574 | 455 | 7,706,192 | 225,360 (2.92%) |

The byte totals are **requests**, before upload-cache reuse, and exclude immediate
draws without a logged source stream. They are neither bytes actually copied nor
a speedup prediction. These are three captured frames, not representative
averages of each map or measurements from the physical Vita.

In the cryo capture, 379 stream requests share their source address/base/stride
with another request. The equivalent counts are 86 and 98 in Blood Gulch. Equal
addresses alone do not prove equal contents, but they highlight the cache
dependency: the current uploader can reuse a sufficiently large snapshot with
the same source after checking its bytes. Rebasing each draw to a different
pointer could turn such reuse into additional copies. The cryo frame also has
86 draws with a nonzero base vertex. Any future rebasing must preserve that
offset, every stream's fetch addresses, generated primitives, constant attributes
and buffer alignment. A reduction in requested range alone is insufficient.

The original `xv_index_copy_bounds` already copies through cached chunks while
finding the maximum. Rebasing cannot simply scan live guest data once and later
publish independently read indices, or rescan uncached GPU memory in every normal
draw. Those would weaken the snapshot guarantee or reintroduce the prior cost.
The new scan remains restricted to explicit diagnostic frames.

Native build and all three requested geometry ownership checks pass: 134, 149
and 574 draws, with zero changed data before completion. The captured run logs
no fence errors, upload failures, draw-storage drops or ordinary Finish calls.
Maximum pending depth is one; upload high water is 1,576/8,192 KiB. Screenshots
record the inspected views. The private emulator is stopped and its prior
executable/configuration restored. No device files were touched.

`tools/analyze_index_ranges.py LOG --output REPORT.json` reproduces the totals
and preserves each parsed draw. The optional diagnostic is also retained in
root `xv_d3d.c`, with all surrounding pre-existing changes preserved. The
authoritative installed and deferred-result staging manifests remain unchanged.

Archive:
`/home/birchwoodgod/xita-backups/2026-09-07-203332-index-range-audit/`.
Native SELF SHA-256:
`a6d35dd57fe97b91c8c1b6ed30f870eea8146720a664ca464cd42df255ddd487`.
Source patch, binary, launch/cleanup records, screenshots, parsed observations
and raw logs are retained. It was not installed.

## Next source task

Continue the user's texture-layout audit. Source inspection confirms that native
BC uploads already use `sceGxmTextureInitSwizzled`, while decoded RGBA map/UI
textures still use `sceGxmTextureInitLinear`. The latter includes paths for
palettes and lightmaps. Evaluate a Vita swizzled layout for supported power-of-two
decoded uploads, preserving every texel, mip level, coverage conversion, opacity
proof and retained-upload lifetime. Keep nonmatching dimensions and render targets
on their required paths. Validate layout independently before changing a tested
candidate, then measure hardware benefit without reducing the saved settings.

The installed scalar/NEON/scalar benchmark and the separate deferred-flare
hardware comparison remain pending. This audit does not establish physical
20 FPS or resolve the untested driving crash.

Follow-up: the [decoded RGBA swizzle candidate](rgba-swizzle-20260907.md) is now
implemented and privately validated; hardware comparison remains pending.
