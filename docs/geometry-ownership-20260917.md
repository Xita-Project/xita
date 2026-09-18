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

The paired `trace-draw` command now queues one recording frame without invoking
the benchmark. It is consumed by the recording owner at a Present/Swap boundary;
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
excluded because diagnostic logging/hashing and packed-layout admission differ
from ordinary frames.

The cumulative diagnostic package preserves the preceding build flags and all
1,586 non-runtime/boot-record package members. Only the D3D HLE and remote-service
objects changed; 92 other objects are byte-identical. Hardware installation was
not attempted when the Vita stopped responding, so remote trace operation on
hardware and its resulting inventory matches remain pending.
