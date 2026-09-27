# Halo CE Universal: applicability to Xita

Reviewed upstream `cybersecurity/halo-ce-universal` at
`5d1ee75a793481baa6add307ea707c86235f9061`. The existing private checkout
remains at `13d1ae224f450dadfbdf53807183e91be5b7d9d0`; only refs were fetched.
No source was merged and the earlier build evidence is preserved.

Its README measurements concern an Intel laptop and a Pixel phone. They do
not predict Vita FPS. Its native Android target is ILP32 AArch64 with GLES;
Xita needs ARM32/GXM. See `native-upstream-build-experiment.md` for the
previous compile probe, SDK requirements and map-version mismatch.

| Upstream change | Xita overlap and next check |
| --- | --- |
| GL state changes only when necessary | `xd3d.c` draw-state cache and `xv_d3d.c` sampler/constant checks already reuse state. Compare remaining per-draw computation, not just the existence of a cache. |
| Persistent copy of Xbox vertex/index memory | Xita has index reuse and retired-slot vertex residency. Check compare/copy bandwidth and invalidation costs; a whole-memory GL mirror is not directly portable. |
| Android three-buffer upload ring | Upstream waits for each frame before reusing its buffer. Preserve Xita's GPU retirement ownership; asynchronous writes do not make in-flight memory safe to overwrite. |
| Once-per-tick sound obstruction | Primarily avoids repeated work when native ports render multiple frames per simulation tick. Xita already runs `XV_SOUND_OBSTRUCTION=6`, reusing ray results under its endpoint/age policy. A second cache is not automatically additive. |
| LTO/PGO | Requires compatible toolchain and representative workload. Their supplied profiles are not profiles of Xita's translated runtime. |

The upstream sound cache keys game time, local-player index and source
position. It does not include the listener position in that key. Any adapted
cache must establish validity for Xita's camera/world changes, not assume
equal sound position proves equal collision queries.

Immediate CPU priority remains the hardware-observed guard action control
(`16D250`) and movement (`15AF50`). The reference actor movement routines
already compare destinations and track whether paths were refreshed this
tick. Measure the callees before deciding another cache is valid/useful.
