# Halo CE world-geometry ownership: first findings

The world geometry has a concrete map-loading lifetime that is separate from
the temporary vertices and visible-triangle indices rebuilt during rendering.
This identifies a candidate for longer-lived native geometry storage, but does
not yet prove that all game writes to its payload have been found. No runtime
validation has been removed and no new FPS gain is claimed.

## Reproducing the inventory

Run against locally owned Xbox cache files; keep the detailed output private:

```sh
python3 tools/audit_halo_bsp_geometry.py /path/to/haloce/maps/*.map \
    --output /private/path/bsp-geometry.json
```

The audit checks the BSP/resource/material relationships, payload bounds,
triangle ownership and every triangle's vertex indices. It rejects unsupported
layouts, offsets or relationships rather than inventing ranges. Output contains
metadata and hashes, not vertex or texture contents. It is an on-disk inventory,
not a runtime write tracker or a performance measurement.

All 24 locally owned maps passed: 82 BSP blocks, 14,642 materials and 2,012,273
triangles. Twelve deliberately damaged metadata/index cases were rejected.
These totals span different levels and mutually exclusive BSP loads; they are
not simultaneous resident-memory requirements.

| Map | BSPs | Materials | Triangles | Render vertex bytes | Lightmap vertex bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Blood Gulch | 1 | 79 | 5,503 | 185,280 | 44,272 |
| Battle Creek | 1 | 65 | 3,317 | 135,744 | 32,344 |
| Pillar of Autumn, all BSPs combined | 9 | 1,866 | 364,774 | 19,296,384 | 4,813,392 |

The existing map inspection helper incorrectly treated the Xbox BSP header as
32 bytes with its signature at `+0x1C`. It is 24 bytes, with the signature at
`+0x14`; the following bytes are already part of the first resource. The helper
now validates the correct header and names its two resource arrays. Existing
collision-query tools still receive the same `sbsp_struct_addr` value.

## Boundaries verified in the owned 3925 executable

| Boundary | Evidence | Implication |
| --- | --- | --- |
| BSP load | `0x35510` waits for file completion, then calls `0x33860` at `0x3555F` | Registration occurs after the new block has been loaded |
| BSP resources | `0x33860` walks count/address pairs at header `+4/+8` and `+0xC/+0x10`, in 12-byte steps | These are render/lightmap D3D resource headers, not vertex arrays themselves |
| Registration | Calls at `0x33882` and `0x338B2` pass base zero to `0x184AB0` | Current HLE resolves Data to a physical address with mask `0x03FFFFFF` |
| Material binding | `0x7A2F0` binds the two material streams through `0x183AD0`; `0x7A3D0` binds one | Material resource pointers and the stride table identify submitted payloads |
| Temporary vertices | `0x7A630` locks a range from a separately allocated buffer and returns its writable pointer | Locking/dynamic geometry cannot be treated as map-resident immutable data |
| Visible indices | `0x53FA0`, already handled natively, expands the current visible triangle selection | Index contents remain camera-dependent even when positions are unchanged |
| BSP replacement | `0x58CD0` calls unload `0x35380`, then load `0x35510` | Cache identity must include the load generation, not just addresses |
| Unload ordering | `0x35380` calls `0x337F0`, which invokes Resource_BlockUntilNotBusy on both arrays | See the GPU lifetime caveat below |

The resource/material correspondence was checked against every owned BSP.
Material render/lightmap descriptors begin at `+0xB0/+0xC4`; their resource
pointers are at `+0xC0/+0xD4`. The executable's stride table at `0x1E0AB4` agrees
with the ranges; ordinary BSP layouts here use 32-byte render records and
8-byte lightmap records. Nine relevant direct call targets and eight instruction
intervals were independently decoded from the owned XBE, beyond inspecting
comments in generated C. No disassembly or owned payload is included here.

## Two lifetime hazards established

All 82 BSP blocks end at guest address `0x819A6000`: loading another block reuses
overlapping memory. The owned maps contain six examples where the resource
address, physical Data address, stride **and byte size all match**, while the
payload hashes differ. Address/size identity cannot safely replace validation
across loads.

The current `D3DResource_BlockUntilNotBusy` HLE returns immediately. That is
compatible with the current renderer retaining its own snapshots; it does not
establish retirement for GPU reads directly from guest memory. Any future
persistent native geometry allocation needs its own draw references and final
GPU retirement before reuse. The original unload call alone is insufficient.

## Next implementation decision

Match runtime stream requests to this inventory by resource, physical range,
stride and active BSP generation. Determine how much measured preparation time
actually belongs to these world streams, versus models and temporary effects.
Then trace payload writers during the loaded lifetime, including initialization,
retained pointers and aliases. These results decide whether to introduce a
persistent BSP snapshot or a narrower reuse scope within a proved read-only
rendering interval. Keep existing exact validation for every unproved class.

This is the prerequisite for removing repeated work safely; it is not a claim
that the world vertex buffers account for all current stream-preparation time.
The active objective remains stable 20 FPS on physical Vita with rendering and
gameplay correctness preserved.

## Runtime trace matching

The paired `trace-draw` command now queues one frame of vertex/index range
records without invoking the benchmark. It is consumed by the recording owner at a Present/Swap boundary;
the network thread does not touch guest state. Normal and sanitizer checks cover
the production HTTP service and actual frame-selection functions, including
manual-trace overlap, counter wrap and an absent networking hook.

After verifying the request/completion markers in a captured game log, match it
against the known map and active BSP:

```sh
python3 tools/match_halo_bsp_draws.py /path/to/haloce/maps/bloodgulch.map \
    /private/path/draw-trace.log --output /private/path/bsp-draws.json
```

For a map with several BSPs, specify the known active `--bsp N`; the tool refuses
to guess. A match requires the resource address, physical Data address, stride
and the complete requested extent to agree. The report separates repeated
requested spans from their union and leaves every unmatched source unclassified.
Matching provenance alone is not evidence of unchanged bytes. Trace timing is
excluded because recording the observations adds work.

The first hardware capture used the full histogram and was followed by a match
networking error. Full diagnostics include vertex hashing, transformed-position
dumps and a packed-layout bypass, which are unnecessary for source matching.
The remote request now selects only range records; manual full histograms retain
their original behavior. Tests verify that remote capture never enables the full
histogram, including repeated requests and frame-counter wrap.

The replacement was installed and its running executable hash verified on the
Vita. Two range captures completed, followed by camera movement and assault-rifle
fire in the same match. Both capture windows and their mixed reporting windows
are excluded from performance comparisons. The package retains all 25 cumulative
build selections and all 1,586 non-runtime/boot-record members. Three objects
changed for tracing; 91 other objects are byte-identical to the preceding
cumulative gameplay build.

## Model resource mapping

`tools/audit_halo_model_geometry.py` checks the separate model resource table
against model geometry parts, including the complete vertex payload extent.
All 24 owned maps passed: 10,862 resource/part pairs and 76,364,000 vertex bytes
across the collection. Each resource has one part; every observed layout is
type 5, stride 32, with zero vertex offset. Seven corrupted pointer/count/layout
cases were rejected. These are loaded-model inventories, not proof that the
payload cannot change after loading.

The draw matcher now classifies both these model resources and the selected
BSP. Unknown sources remain unclassified. Resource, Data, stride and complete
extent must match; four deliberate mismatches were rejected. Halo's compressed
model vertices can be transformed/skinned using shader constants without
changing these source records; the inventory alone does not establish which
runtime writers exist.

## First hardware range results

These are two Blood Gulch views in one freshly launched solo match at native
resolution with existing graphics settings. Counts include repeated requests
before cache reuse; they are not transferred bytes, saved time or GPU draw counts.
Immediate draws without a source stream are excluded from the byte totals.

| View | Traced draw records | BSP requested bytes | Model requested bytes | Unclassified bytes |
| --- | ---: | ---: | ---: | ---: |
| Facing the base | 237 | 659,664 | 415,328 | 2,016 |
| Looking along the valley | 224 | 525,440 | 427,104 | 1,312 |

For the base view, the union of requested BSP ranges is 179,936 bytes; repeated
passes request 659,664 bytes from those resources. The valley union is 154,200
bytes versus 525,440 requested. The model unions are 313,536 and 294,464 bytes.
Over 99.8% of requested bytes matched map-loaded resources in these two samples.
Existing exact validation already reuses many uploads, so this finding identifies
repeated validation as a candidate, not new copy savings or an FPS gain.

Next, audit runtime writers and lifetime boundaries for both families. A
generation-owned native geometry cache must account for map/BSP replacement,
retained writable pointers and GPU retirement before it can bypass validation.
Keep the current owned snapshots for unproved sources. Stable 20 FPS remains the
goal; these diagnostic changes do not establish it.

The [write-lifetime follow-up](geometry-write-lifetimes-20260918.md) now pins
the BSP/model registration and drain routines and demonstrates retained
temporary pointers by executing the original pool helpers. Lock-only tracking
cannot establish resource immutability. The next cache admission work remains
specific to BSP/model payload writers and their load generations.
