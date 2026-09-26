# A30 transition and missing Warthog body

User reported a black screen near the Warthog section of a30, probably during
a loading transition. Captured the running device without input or restart.
Private evidence: `../crash-a30-warthog-black/20260926T224739Z/`.

Confirmed build perf260 / 97c49789, active slot 1, runtime SHA beginning
83b9e5ed. Frames advanced from 18278 to 18780 across retrieval and follow-up;
the device was responsive. `screen.png` shows active outdoor gameplay with
the Warthog wheels and rider visible but its body absent. This is evidence of
a rendering problem after recovery, not proof of a persistent black-screen
crash or of its cause. No restart or rollback was performed.

Near the first log tail:

- At approximately 1297 seconds, the isolated a30 test namespace reads
  3,428,352 bytes from `cache/savegame.bin`.
- The generic large-file-read hook requests a texture-cache purge.
- Scene 13728 reports 684 ms dispatch-to-completion, last HLE D3DDevice_End.
- CPU/guest telemetry continues after the event.

Important interpretation: `xk_file.c` emits "map tag data loaded" for any
read larger than 1 MiB. Here the preceding read is a savegame, so that message
alone does not prove a BSP/map transition. Checkpoint restore and its cache
invalidation are investigation targets; do not equate the log label with
confirmed map streaming. No causal link to the missing body is established.

Requested one draw-trace frame after recovery to distinguish missing draw
submission from incorrect material/texture state. Its timing must not be used
as a gameplay FPS sample. Compare any actual vehicle draw with the cloned
reference's model rendering, effect inheritance and lighting-cache behavior,
while preserving retail build layout differences.

## Trace receipt defect

`draw-trace.log` contains frame 18406's buffered payload (163,093 bytes,
zero buffer-dropped lines) but no `hist: remote render frame ... closed`
receipt. The strict summarizer rejects it as incomplete. Do not manufacture
a closing marker or claim the recorded commands account for the whole frame.

The cause is visible in the logging path: remote begin/end receipts use
D3DLOG -> xk_os_log -> xv_logf; ordinary logs from scene/recording workers are
suppressed. The draw payload itself uses the critical sink. Updated only the
two remote boundary receipts to use a weak critical logger with host fallback.
Per-draw logging remains buffered, and general worker suppression remains.

`SANITIZE=1 python3 tools/test_remote_draw_trace.py` passes the actual selector
functions with critical logging, absent critical sink, absent remote hook and
suppressed ordinary worker logs. The test retains overlap, queued/repeated
requests, empty frames and counter-wrap checks. This fixes diagnostic receipt
delivery, not the Warthog rendering itself.

Private perf261 candidate is being built in `../draw-trace-receipt-candidate`,
derived from perf260 with only xd3d receipt logging and version/revision
changes. Build handle 21937 must be polled until terminal; no deployment yet.
Preserve the user's current campaign progress before any later restart.
