# Exact visibility publication at an existing scene boundary

Private experiment based on `138c434`. Both compilation (`XV_QUERY_BOUNDARY=1`)
and runtime admission are opt-in; the runtime starts OFF. No guest lift or shader
regeneration is required. This changes when the pump publishes completed query
results, never when frame resources are released.

The physical placement census on runtime `9910a0db` found a boundary after command
44 in scene 1 in the stationary native Blood Gulch red-base view, with about 77
remaining draws and eight scenes. Those are command counts, not GPU durations or
an FPS prediction. Other views must establish their own admission and timing.

## Exact boundary and ownership

`xv_d3d_query_boundary_prepare(frame, enabled)` is called by the pump after the
packet is sealed and acquired, before scene submission. It scans the entire list
before accepting a prefix: every kind-0 command with a nonzero visibility slot is
a potential writer, including zero-index or subsequently skipped draws. It does
not deduplicate numerical query IDs, result slots, or repeated views. Tagged clear
commands cannot write visibility. All command/UI targets and bounded slot records
are checked before acceptance; an open query, bad bound, invalid serial/slot,
unknown kind, missing target or unordered UI marker declines the candidate.

The planner then simulates the existing UI-before-command replay and its final
RTT-to-backbuffer transition. It selects the first existing target-transition
EndScene with an exclusive command cursor strictly after the global last potential
writer. Runtime attachment requires the exact frame, command and UI cursors, once
only. A packet without queries, without writers, or without such an intermediate
EndScene falls back. In particular, no-query/zero-writer packets are not published
speculatively during CPU preparation.

`xv_d3d_query_boundary_arm` copies the notification into a fixed per-list record.
The attached argument is the **fragment** notification of the original EndScene.
No BeginScene, EndScene, Finish, draw, clear, viewport or UI command is inserted,
removed or reordered. Existing synchronous diagnostic/error Finish calls remain.
The proof relies on the renderer's existing single-context fragment ordering and
render-target replacement drain; no target or packet may be replaced while owned.

The candidate reuses the existing *separate query notification bank*. Its word is
initialized to the complement of the ticket, including ticket zero on wrap. The
final fence address/value is untouched. When a prefix is admitted, the old scaled
world notification is omitted from that packet to avoid writing the same query
word twice. If admission declines, the selected scaled-world path remains intact;
otherwise completion falls back to the original final fence. Scaled-world mode is
not changed by this comparison.

Only `xv_pump_retire` publishes, in packet order, through the unchanged
`xv_d3d_visibility_complete`: original four-core sum, scaling, issued-slot order,
exact generation history, zero/no-buffer behavior and scheduler notification.
The pump cannot observe a prefix until CPU submission returns. A signaled younger
packet is not published ahead of the oldest. Submission errors still drain accepted
work with the original Finish before fallback publication and retirement.

Early publication never advances `g_frame_completed`, checks/releases geometry,
frees UI snapshots, resets visibility storage or permits notification-word reuse.
The per-list descriptor and per-ticket words remain owned until original final
retirement. Plans are cleared for every subsequent real mesh packet even in
runtime OFF mode, preventing stale frame-wrap reuse. Existing flare brightness,
identity/reset and Present fences remain unchanged; the four-generation history
retains its original consumer-lifetime contract.

## Control and evidence

Selector **39**, remote name `query-boundary`, uses the existing owner Present drain
and stationary-view controller: 60 settle and 120 measured frames for OFF/ON/OFF.
It restores the exact initial OFF or ON state on completion, cancellation or lost
view. Uncompiled/unavailable candidates reject admission. Graphics, camera,
workers, pipeline and old world-fence selection remain unchanged. The old
`early-visibility` comparison rejects an already-enabled prefix candidate because
it would mask that comparison. Selectors 36/37/38/294 retain their meanings.

Every 60 original final retirements, two bounded rows report:

- `[query-boundary]`: OFF/ready/no-query/no-writer/no-boundary packets, six decline
  reasons, and successful/failed existing-EndScene attachments.
- `[frame-query-boundary]`: observed prefix notifications, the subset sampled while
  the final word was still pending, and final fallbacks for admitted packets;
  query latency and remaining final tail are scheduled CPU observations.

The explicit before-final count uses one additional final-word load only when a
prefix is observed. A positive reported CPU interval alone is not treated as proof
of pending GPU work. The rows have a final-retirement cadence, so an OFF/ON transition
can cross a report window: use controller phase markers and steady interior
windows, not one boundary row. These counters do not establish an FPS benefit.

Compiled OFF emits the original renderer `.text` (verified against the base).
Compiled ON/runtime OFF performs an atomic mode read, clears one 32-byte plan,
increments a counter and checks the disabled attachment at original RTT ends.
Enabled planning is bounded by 2048 commands, 1024 UI markers and 512 query slots;
there are no allocations, packet copies or added per-command clocks. Plans occupy
96 bytes, reason counters 44 bytes and attachment counters 8 bytes on ARM, plus
small pump counters and packet fields. Object `.bss` deltas include padding and
are not `sizeof` totals. The separate placement observer is 832 bytes if compiled;
it is unnecessary in the intended comparison package.

## Validation and limits

`python3 tools/test_query_boundary.py` exercises the production RTT replay and
completion/history implementation under ASan/UBSan, plus actual benchmark state
and production pump code. Cases include multiple targets/UI/views, repeated IDs
and later writers, zero-index writers, empty final scenes, no queries/no writers,
invalid/unsupported records, Begin/End failures, full fixed storage bounds, frame
and serial wrap, GPU-unavailable exact zero, original result-ID exhaustion and
OFF/ON trace identity apart from the new notification. Normal, cancelled and
lost-view controller runs restore both initial states; absent APIs reject 39.
Pump fixtures delay query/final/display independently, suppress the query signal,
exercise failure drains, native/scaled/legacy fallback, pacing and ticket wrap,
and check that early publication does not release packet ownership. They run with
GPU packet timing both omitted and enabled.

`TEST_QUERY_BOUNDARY=1 python3 tools/test_frame_acquisition.py` exercises the actual
slot acquisition and drained comparison override, including 200-frame slot reuse
and ticket wrap. Ordinary OFF acquisition/completion, placement observer and all
seven RTT lifecycle modes also pass. `python3 tools/test_remote.py` verifies the
real loopback HTTP route, status 39, exclusion and parser. These are compositional
host tests: GXM and GPU memory writes are simulated, not a physical GPU oracle.
There are no new threads; no TSan or hardware run is claimed.

ARM compilation passes for main/renderer/controller/remote in OFF and ON builds,
and for the intended `GPU_PACKET_TIMING=0`, `VISIBILITY_PLACEMENT=0` package. The
planner has 40 bytes of static compiler-reported stack use; report 56, RTT replay
136, and pump 136 in that package (OFF pump 128). These are individual functions,
not a complete linked call-chain high-water measurement. Incremental OFF/OFF/ON/ON/
OFF stamps and rejection without `RECOMP=1` pass.

## Integration and physical comparison

Cherry-pick the experiment, stage the changed runtime/header/Makefile and client
files, and rebuild with the existing retained flags plus `XV_QUERY_BOUNDARY=1`.
No owned generated guest unit changes. For the proposed package leave
`XV_GPU_PACKET_TIMING=0` and `XV_VISIBILITY_PLACEMENT=0`; keep unrelated candidates
at their original modes. A rebuilt process still starts with the candidate OFF.
The parent owns packaging, startup/logger shutdown checks and hardware actions.

After the parent verifies the installed package and a stable native camera:

```sh
python3 tools/vita_remote.py --config PRIVATE_CONFIG benchmark OUTPUT_DIRECTORY \
  --kind query-boundary --runs 3 --timeout 240
```

Require valid camera and restoration, ready/attachment evidence in ON, no unexpected
errors or final fallbacks, observed-before-final counts, exact flare behavior,
query-wait and whole-frame/FPS results. Then test changing views and broader gameplay
for fallback and lifetime behavior. Record the package flags and runtime hash.
An admission count or shorter query latency is insufficient to claim a frame-time
win; resolution already changes the bottleneck substantially in this view.
