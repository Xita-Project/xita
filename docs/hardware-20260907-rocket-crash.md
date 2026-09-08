# September 7: rocket self-damage GPU crash

The user picked up a rocket launcher and fired at themselves to see the body
fly. A new GPU crash occurred on the unchanged vertex-comparison executable,
not on the pending flare or frame-constant candidates. USB collection:
`/home/birchwoodgod/xita-backups/2026-09-07-220547-rocket-crash-hardware`.

The 1,643,184-byte `psp2core-1788836281-GPUCRASH.psp2dmp` hashes to
`99b52232fa7504d374b8b82e7a98aee30a4012b24f9c5d125c8e60c1b82e45ab`.
The log ends in a shader link against `halo_vs_10`, and the dump's TTY confirms
`appmgr_aborthandler.c(793) render gpu crash`. No CPU thread reports a data
abort. Application global frame packets are absent from the dump; the final
shader log is not enough to identify the faulting draw.

## New register evidence

`tools/analyze_vita_gpu_crash.py` decodes the existing GPU and memory-block
notes. It is read-only and leaves the unknown trailing GPU data undecoded.
The layout comes from [vcp](https://github.com/isage/vcp/blob/master/lib/src/info_gpuinfo.cpp)
and its [memory-block parser](https://github.com/isage/vcp/blob/master/lib/src/info_memblockinfo.cpp).
Register masks come from the published [SGX543 definitions](https://github.com/GrapheneCt/PVR_PSP2/blob/master/include/gpu_es4/eurasia/hwdefs/sgx543defs.h).

| Dump | GPU core(s) | BIF status | Fault page |
|---|---|---|---|
| Earlier driving crash | 0 and 3 | `00090400` | `70400000` |
| Rocket/death crash | 3 | `00090400` | `70400000` |

In both dumps the `xv_vertex_ring` allocation starts at `70200000`, has size
`00200000`, and ends exactly at `70400000`. The next CPU allocation is the
GXM render-target driver block. Request mask is `0400`, fault type field is 1.
These values identify a repeatable memory-boundary lead. They do **not** prove
which shader access crossed the boundary, or exclude an invalid access into
the neighboring driver's allocation. Do not call this a proven ring-wrap bug.

The crash run has single-flight enabled, ordinary Finish counts zero, and no
reported vertex upload failure in the last completed windows. The later
[360p FPS session](hardware-20260907-fps-followup.md) has no additional dump.

## Targeted candidate

`/home/birchwoodgod/xita-backups/2026-09-07-221549-frame-constants` is based only
on the installed build. Mesh constants are copied once into each protected
frame slot and bound with `sceGxmSetVertexDefaultUniformBuffer`, bypassing the
vertex default-uniform ring for compatible layouts. All 67 compiled Halo
vertex programs were checked to expose a complete float4 `c[]` at offset zero.
Runtime reflection verifies this contract; packed overrides retain SDK packing.

Each slot adds 1 MiB of uncached, GPU-readable storage plus one mapped tail
page (about 3.01 MiB total). Slot reset follows existing fragment-notification
retirement. The pump publishes the captured constants once and reuses them
across draws and render-target/UI boundaries. Missing snapshots, invalid spans,
allocation failures and failed uniform API calls reject the draw rather than
submitting it with stale constants. No new per-frame Finish call is added.

Host tests exercise immutable slot ownership, 12,800 byte-exact random window
comparisons, complete-pool boundary, float/NaN bit preservation, shader layout
rejection, allocation/mapping/binding failures and the disabled path under
ASan/UBSan. Existing frame-acquisition, retirement and render-target checks
pass. Native build succeeds. Private emulator checks passed for menu, Blood Gulch,
two close grenade explosions, death with the body visible, respawn, campaign
opening and skipping to cryo. Four ownership captures checked 75/2/520/531 draws
with zero mutations before GPU completion. The largest constant upload was
444 KiB; no direct-binding rejection or SDK-layout fallback occurred. These
checks did not reproduce the specific rocket shot on hardware.
**Hardware crash clearance and performance improvement remain unverified**.

## Delivery at 22:33 CDT

The candidate was written through the existing 30,891,526-byte executable
allocation, with a full local backup, direct device readback, fresh read-only
mount verification, and safe unmount. Installed SHA-256:
`82b21c788717ed663d7cfb691e7b7f1985f16ac85720261322e7475c215d17d0`.
All 1,655 other tracked files remain byte-identical, including the latest logs,
saves and the user’s 360p configuration. No VPK or new card file was needed.
The next hardware test is rocket self-damage, the visible body, and respawn;
then a short ordinary Blood Gulch play session. Performance comparisons follow
a stable crash retest. The installed candidate has not yet produced hardware
measurements.
