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

Build 21937 and packaging 85852 completed successfully. Prepared
`draw-trace-receipt-candidate/xita-perf261c.vpk`, version 0.2.0-perf.261,
revision 9fcf7486. Runtime SHA-256
e18a9ee9209efd7026dd826c6f94988b664a5e2ed6d7ed0343aa50f9275f722c
(34,805,526 bytes); package SHA-256
1d65ebcd0e443e685fdeb88d643d8ad5a5e5eded82407893cc9cfe0af8926c89.
Entry comparison against perf260 changes only game-a.self and boot-game.txt;
update contract is unchanged. Not deployed; current campaign progress was
not interrupted. This candidate establishes no rendering/performance gain.

## Perf261 deployment and hardware receipt verification

The user subsequently saved and quit. Deployment completed with verified bytes,
slot 0 and boot confirmation; live status also reports perf261 / 9fcf7486.
The protected a30-perf211 launch sequence completed. Several screenshots still
showed loading while map data was copied; those timings are excluded. Eventually
`draw-trace-receipt-candidate/gameplay.png` confirmed lifepod gameplay, not the
Warthog section. Thus this run does not reproduce or resolve the missing body.

`receipt-trace.log` passes the strict `tools/summarize_draw_trace.py` parser:
frame 4954, 396 commands, 239 draw-state records, 343881 buffered bytes, zero
dropped lines and a matching closing receipt. The one-shot trace was requested
while loading was progressing, so the later gameplay image is not a synchronized
image of trace frame 4954. No timing claim is made for the trace frame.

The 154066FD material family accounts for 101 recorded draws (80 vs09 and 21
vs27). This is not proof of shadow work, screen coverage or GPU cost. Full
material/target/blend classification is necessary before an optimization.

The owned a30 map inventory also completed: 789 vertex resources / 5,091,776
vertex bytes, including 51 Warthog parts. Private resource identities are saved
in `draw-trace-receipt-candidate/a30-model-resources.json`.

An additional source change routes stream identity to the bounded trace buffer
while a remote trace is active. Previously those ordinary worker log messages
were suppressed. It captures frame/command/stream, guest resource, resource
data, count, stride and base vertex while the recorder owns guest state. It
does not read back vertices or hand guest pointers to a worker. Rejected commands
can leave provisional stream records, so future consumers must correlate only
accepted draw records and account for reused command indices.

The modified renderer compiled with the candidate's actual Vita build flags
(`geometry-compile.json`, return code 0). It is not linked, packaged or deployed;
perf261 still lacks these added stream records. Next: package the diagnostic
extension, capture a vehicle scene with resource identity, and distinguish
missing submissions from incorrect state before changing geometry/materials.

## Perf262 candidate

Built successfully in the private `../model-identity-candidate/build-x87`
stage, derived from perf261 with only the model-stream diagnostic source and
version/revision metadata changed. Version 0.2.0-perf.262 / a0fc8cba.
`xita-perf262c.vpk` differs from the perf261 package only in `game-a.self`
and `boot-game.txt`; the updater contract remains unchanged.

- Runtime: 34,805,274 bytes, SHA-256
  `92868028ca11caf15158846d0fbc2a51fb2a4eb5ec8924f64c0f61545828b6eb`.
- Package SHA-256:
  `3cd639e6a4232de25d6a8829a9fb5aee6f9fe95a482510fe6f5693d303a17d0a`.

Deployment started under tool session 6854, logging to that private directory's
`deploy.log`. Do not infer boot success from completed upload alone; inspect its
final receipt and live status. No additional performance changes are bundled.

The latest settled perf261 log reports roughly 247 deferred draws/frame and
36 drain calls/frame; only about 1.7–1.9 drains/frame waited, for approximately
1.0–2.3 ms/frame in several late windows (earlier windows reached ~5 ms).
Therefore removing all 36 calls cannot be credited with a large frame-time
saving. Many return immediately on an empty queue; nonempty drains protect
recorder/guest-state ownership. Further changes need a specific dependency
that can be removed or delayed without violating that ownership.

Deployment session 6854 subsequently exited successfully. Live status confirms
perf262 / a0fc8cba on a fresh dashboard boot (timing frame 0), and its awake lease
was renewed for 3600 seconds. The temporary connection refusal occurred during
restart and recovered without a second deployment. Starting the same protected
a30 launch sequence for this build; exclude loading and trace frames from timing.

## Perf262 model identity verified on hardware

Launch session 33496 completed. First requested frame 4449 captured loading
(3 commands / 2 draws), so it is excluded from gameplay analysis. Later screen
`model-identity-candidate/gameplay-before-trace.png` confirms lifepod gameplay;
status timing frame 6078 preceded the second request. Its immediately retrieved
log still contained only the old receipt: do not accept an old completed trace
as completion of a new request. Follow-up retrieval `gameplay-trace-complete.log`
contains matching closed frame 6081: 402 commands, 244 draw records, 372430 bytes,
zero dropped lines. The strict summarizer passes.

Private map/resource correlation in `model-matches.json` identifies 138 stream
records, with 109 unmatched (these are not automatically invalid: BSP and dynamic
streams are outside the model-resource inventory). All matched records reference
accepted draw commands and no matched command/stream pair is duplicated. This
establishes model-resource identity in this capture, not visible pixel output,
object-instance identity or an unnecessary draw.

`model-pass-counts.json` joins stream 0 to accepted render-state records. Largest
identified groups include 29 armored-Marine draws, 15 pine draws and 12 pistol-ammo
draws, all 154066FD, pass 0, blend disabled. First-person AR also has 10 additive
FEBED18F draws. Alpha test, material multiplicity, multiple objects and lighting
passes remain relevant: do not merge draws based only on a shared resource.
The observed dominant Marine group is not evidence of blended-particle overdraw.

The private resource inventory now includes shader tag names using the existing
Xbox model layout (shader table +DC, part shader index +4); Warthog hull, tires,
windshield, lights and instruments can be distinguished in a later vehicle scene.
No Warthog is in this lifepod capture, so its body bug remains unresolved.

Next performance work should inspect reuse of model preparation/material state
for these actual model classes without changing draw order or object transforms.
The diagnostic update itself establishes no FPS gain. Loading also remains slow:
the captured log shows cache output progressing past 196 MiB, with approximately
11 seconds cumulative write time; neither that figure nor CPU utilization alone
isolates decompression, scheduling or the total load duration. Upstream zlib
decompression is a reference lead, not a proven loading bottleneck or FPS fix.
