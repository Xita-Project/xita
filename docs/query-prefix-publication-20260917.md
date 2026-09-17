# Ordered query-prefix publication prototype — September 17

This default-OFF Halo CE candidate lets a younger frame publish its completed
visibility-query prefix while an older frame's final rendering tail remains
in flight. Final packet retirement, GPU/display synchronization and every
geometry, texture and UI slot owner remain unchanged. It creates no extra
GXM scene or notification and never substitutes an older query result.

The existing pump examines queries only at `g_frame_completed + 1`, then
stops if that packet's final fence is incomplete. The new pump-only scan runs
after this original retirement loop and visits submitted packets in ticket
order. An older unpublished query, invalid owner, failed packet, unavailable
prefix or history collision stops the scan. The existing oldest-packet and
final-fallback code handles those cases.

The saved gameplay logs establish substantial exact-result waiting in some
views, but do **not** establish that a younger prefix actually completes before
an older final fence on hardware. This is a qualified scheduling prototype,
not a demonstrated FPS improvement.

## Selection and scope

Explicit build selection:

```
make RECOMP=1 GAME_PROFILE=halo_ce_3925 \
  XV_QUERY_BOUNDARY=1 XV_QUERY_BOUNDARY_DEFAULT=1 \
  XV_FLARE_QUERY_OVERLAP=1 XV_QUERY_PREFIX_PUBLISH=1
```

Omitted/zero `XV_QUERY_PREFIX_PUBLISH` preserves the old generated objects.
The selector validates 0/1 and rejects missing recompiled boundary/history
support or a different game profile. Only `runtime/main.o` and
`runtime/xv_d3d.o` consume it. Both incremental transitions are tracked.

The query-boundary path must actually be enabled at runtime; a compiled
candidate with no accepted existing prefix correctly retains the old path.
No new dashboard setting or benchmark mode is introduced.

## Exact-generation and ownership argument

1. Only fully submitted packets are scanned. Ticket/fence identity is checked;
   traversal is bounded by the three frame slots and four ticket records.
   No recording list or stale notification word is accepted.
2. Query publication stays ordered. Later packets cannot overtake an older
   unpublished query. Queryless/UI-only packets have no query generation to
   publish. Failed/unproved query packets retain the original final path.
3. `visibility_completed` guarantees one publication per packet. Final
   retirement skips republishing completed prefixes and retains its original
   geometry checks, ownership release and ticket advance.
4. A separate 256-byte bitmap marks history buckets referenced by every older
   still-owned packet. A younger publication is declined if any of its
   result-slot/serial buckets would collide. Counting frames alone is
   insufficient: repeated IDs or serial wrap can collide within the existing
   four-generation history even with only two frames in flight.
   Immutable history declines are cached only until the oldest completed
   ticket changes, then re-evaluated. Submission clears the cache flag.
5. Retained generation consumers in this Halo runtime are the deferred flare
   reader in `recomp/kernel/xk_flare.c`. Both real
   `xv_hle_D3DDevice_Present` and `xv_hle_D3DDevice_Swap` call its Present
   barrier before requesting another frame. A drain parks another guest fiber
   while it owns pending writes. Consequently a reader cannot retain an
   arbitrarily old retired generation while later frames continue submitting:
   the next Present consumes it first. The new bitmap additionally preserves
   every still-owned older packet's history, including synthetic delayed
   consumers. This is not a new promise of unlimited history lifetime, and
   is why the feature is restricted to this game adapter.

The scan never modifies `g_frame_completed`, slot owners, display release,
query issuance or result-history layout. The bitmap uses sealed packet metadata,
not mutable guest query-issue state. All paths keep the original result
publisher and GPU-completion proof.

## Validation

`python3 tools/test_query_publication.py --out /private/evidence/host`
extracts the actual production packet definitions, scan, retirement body,
submission reset, completer and history guard. It supplies synthetic GPU
notification words and minimal list storage, not a second implementation of
the scheduling policy.

- 17 scenarios across OFF/ON and packet timing OFF/ON: **68 ASan/UBSan runs**.
- Older final pending / younger prefix ready; out-of-order readiness; three
  delayed generations; history collision; ID/serial/ticket wrap; queryless
  packets; missing/unproved prefixes; failed packet; invalid owner/bounds;
  all 512 result slots; production submission reset and one publication.
- Three negative controls fail at assertions: ignore history collision,
  skip an older incomplete prefix, and retire storage on prefix publication.
- The existing actual full pump-loop fixture passes **102 cases** with the
  feature ON and packet timing OFF/ON. That fixture models noncolliding
  generations; the history guard is exercised in the first suite.
- Six actual Vita SDK Make invocations cover default -> explicit OFF -> ON ->
  repeat ON -> OFF -> repeat OFF. Only the two owners rebuild on transitions;
  repeats are no-ops. Both OFF objects exactly match the retained runtime.
- Eight invalid-selector/prerequisite configurations reject before compilation.

Reproducible full pump fixture:

```
TEST_QUERY_BOUNDARY=1 TEST_QUERY_PREFIX_PUBLISH=1 TEST_GPU_PACKET_TIMING=0 \
  python3 tools/test_frame_completion.py
TEST_QUERY_BOUNDARY=1 TEST_QUERY_PREFIX_PUBLISH=1 TEST_GPU_PACKET_TIMING=1 \
  python3 tools/test_frame_completion.py
```

Actual retained-flag ARM compilation adds 332 bytes of main `.text` and
292 bytes of D3D `.text`; total read-only size including literals rises by
768 bytes. Main BSS rises by 48 bytes; D3D BSS and data are unchanged.
The pump's compiler-reported local stack frame changes from 136 to 144 bytes;
the nonrecursive history guard uses 304 bytes, including its bitmap.
The existing visibility publisher remains 96 bytes. These are compiler local
frames, not a measurement of the complete native-thread stack high-water.

The new 60-retirement report counts younger prefixes published, publications
observed with an older final still pending, and distinct history-blocked
packets. Those counters can show eligibility during ordinary play without
forcing a benchmark. A positive count is not itself an FPS gain.

Private evidence is under
`validation/engine-restructure-20260914T2300Z/direct-cluster-query/query-prefix-publication-prototype/`.
It includes commands, object hashes, sanitizer/negative-control output, Make
transitions and actual ARM stack reports. Owned headers used to reproduce the
retained shader ABI stay in the private build stage. No package was deployed.

