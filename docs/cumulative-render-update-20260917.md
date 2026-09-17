# Cumulative rendering and query update — September 17

Source `79b067c` retains all twelve preceding gameplay selections and adds
three compatible candidates. Their repository defaults remain OFF; the private
hardware gameplay package explicitly enables them together.

- [Packed vertex layouts](packed-vertex-layout-prototype-20260917.md) copy and
  validate the 16 consumed bytes from eligible 32-byte records for three exact
  shader programs. The GPU uses an alternate stride with the same program
  bytes and fetched attributes. Unsupported layouts retain the original path.
- [Query arithmetic reconstruction](query-semantic-leaf-integration-20260917.md)
  keeps one qualified distance calculation in scalar VFP locals, then restores
  the complete observed SSE state. Guest writes, precision, callback boundaries
  and the existing native-trap admission gates remain unchanged.
- [Ordered query publication](query-prefix-publication-20260917.md) can publish
  an already completed younger query prefix before an older rendering tail
  retires. It protects retained history buckets and leaves all final buffer
  ownership and GPU/display retirement unchanged.

These are cumulative experimental changes, not three proven FPS gains. The
query prototype saves roughly 2% of its modeled whole-query instruction count;
that is not 2% of a hardware frame. Packed layout savings depend on the shader
mix and cache hits. The new publication path helps only if hardware actually
finishes a younger prefix before an older final fence. Passive counters record
that opportunity during ordinary play.

## Build and package checks

The retained twelve-path Make command has exactly three added selectors:
`XV_PACKED_VERTEX_LAYOUT=1`, `XV_QUERY_SEMANTIC_LEAF=1`, and
`XV_QUERY_PREFIX_PUBLISH=1`. No graphics setting or global optimization flag
changes. The final stage changes seven objects: the qualified query unit and
six rendering ABI owners. The other 86 objects remain byte-identical. All
caller, generic game, collision-solver and shared shader assets are retained.

The final query object exactly matches the independently reviewed production
object `13684fe2…`; the unchanged solver retains its qualified section hash.
The package has the same 1,588 members and updater contract; only
`boot-game.txt` and `game-a.self` differ from the preceding package. The runtime
SHA-256 is
`f037ddb3dacf51d373adf7a904a30c461c50ddee87574708f3464632df1cef22`.

Private receipts are in `direct-cluster-query/query-prefix-startup/`, with
intermediate package checks in `packed-vertex-startup/` and
`query-semantic-startup/`. Those intermediate packages were not installed.

## Pre-update gameplay evidence

The preceding twelve-path build completed ordinary Blood Gulch camera movement,
a plasma charge/release with a Ghost and Warthog visible, driver entry, forward
and reverse driving with cliff contact, vehicle exit and pause. Both vehicles
remain visible in the charge screenshot. The 6,010,106-byte saved log contains
no searched fault marker, and the logger reports no error or failed writes.

The initial spawn reports 13.4–13.5 FPS, a valley view reports about 9 FPS, and
the driving capture shows 8 FPS. These are different live workloads, not matched
performance comparisons. This short check does not cover rocket death/respawn,
long-session crashes or establish stable 20 FPS. No built-in benchmark or Halo
CE Vita3K gameplay validation was used.

## Installation

The remote updater verified the 31,959,638-byte executable, switched to slot 1,
and confirmed its boot hash. The twelve-path runtime `2393a044…` remains in
slot 0 for rollback. The dashboard appeared and ordinary Launch Game began a
fresh Halo process. The original New001 Normal Pillar of Autumn pistol checkpoint loaded. Initial
passive reports are 12.7–12.8 FPS at the same rounded camera position, versus
12.7 in the preceding fresh-launch build. Live AI and draw counts still vary;
this does not establish a reproducible speedup.

The packed path is active: one 60-frame report has 1,200 requests representing
11,797 KiB of source records and 5,898 KiB of consumed payload. These are
requested spans, not measured GPU writes or saved DRAM traffic. The younger
query-publication counter is zero in the sampled checkpoint windows, so no
benefit from that scheduling opportunity is established there. All fifteen
compatible paths remain enabled while gameplay qualification continues.

The fifteen-path campaign smoke completed two fire inputs, camera/strafe,
forward/reverse movement and pause. All three alternate vertex programs were
created successfully on hardware. The saved 1,940,526-byte log has no searched
fault marker and the logger reports zero errors/failed writes. This is a short
smoke check; it does not settle long-session crashes, rocket death or performance
across the campaign. The captured tail includes paused reports, which are
excluded from active gameplay comparisons.

## Blood Gulch follow-up

The same process returned through normal menus to a solo New002/New003 Slayer
match. Camera/walking, plasma charge/release with both a Ghost and Warthog in
view, driver entry, steering, forward/reverse driving, exit and pause completed.
The charged screenshot retains both vehicles. The driving capture shows 6 FPS;
later settled reports show 11 FPS. The initial different spawn reports
12.8–13 FPS. These are unmatched views/routes, not a gain or regression verdict.
The target remains unmet. A look-down/L input did not capture an explosion or
death, so it does not qualify rocket or death/respawn behavior.

At the final receipt, frame 37,357, benchmark mode is OFF. The saved log is
6,070,808 bytes, with no searched fault marker and zero logger errors/failed
writes. The Vita is paused on foot near the Warthog; input is neutral. This is
functional smoke coverage, not prolonged combat/crash qualification.

Across all 601 publication reports in the saved process log (including menus,
loading, campaign, Blood Gulch and pause), younger publications, older-final
opportunities and history declines are all zero. This is no demonstrated
scheduling benefit in these workloads. It is not a GPU-active-time measure or
proof about every scene. Packed requests remain nonzero. All selected paths
remain cumulative; the next performance work targets CPU query cost rather
than assuming another synchronization win.

## Ordered collision scan follow-up

The next cumulative build adds `XV_QUERY_MEMBERSHIP_SCALAR=1` while retaining
all fifteen preceding selections. Source `231a1af` produces runtime
`d5f4f4eb8f142f37d547a10b3bb2eaca57c7962387e0babfdd78484fce252912`, installed
and boot-confirmed in slot 0. The fifteen-path runtime remains in slot 1.
Standard graphics are unchanged. The updater restarted Xita before the ordinary
gameplay check; this is not a hot toggle or built-in benchmark.

The scalar edge-membership loop reduces repeated translated integer/flag work
while preserving original read order, complete observed state, callbacks and
the enclosing actor transaction. The qualified whole-query fixtures show
roughly 1–4.63% fewer instructions in relevant cases, with small regressions
below 0.3% elsewhere. These are instruction counts, not hardware FPS gains.
The [integration note](engineering/QUERY_MEMBERSHIP_SCALAR_INTEGRATION.md)
records its guarded default-OFF wiring and correctness scope.

The complete package build changes only `query_fusion.o`; rebuilt caller and
solver objects, all other objects, assets and updater contract retain identity.
The query object is byte-identical to the qualified production object. All
1,588 package members remain, with only the executable and boot selection
different. Repository defaults remain separate from this private cumulative
gameplay build.

Fresh-launch New001/Normal campaign smoke completed two pistol fire inputs,
camera/strafe, forward/reverse movement and pause. Initial active reports are
12.7–12.8 FPS at the same rounded checkpoint view, with varying live AI/draw
counts. The saved log contains 1,716,924 bytes at receipt frame 12,676, no
searched fault marker and zero logger errors/failed writes. The Vita is paused
in campaign with neutral input. Paused tail reports are excluded from gameplay
comparisons. All three packed shader variants initialized successfully.

No combined FPS gain, stable 20 FPS, or resolution of historical long-session
crashes is established. Blood Gulch driving and rocket/death behavior have not
been repeated for this scalar follow-up; the preceding fifteen-path smoke is
documented above. Retained texture-binding reuse and early depth preparation
are the next cumulative retest, pending narrow startup/fixture checks.

## Retained rendering options combined

Source `99c93cf` adds narrow startup defaults for the existing exact texture
binding cache and early depth-only preparation. The private package selects
both while retaining all sixteen previous paths. Repository defaults remain
OFF, and explicit environment/config overrides retain their previous behavior.
The [startup note](retained-render-startup-20260917.md) records focused
configuration, descriptor, resolver, recording/replay and boundary checks.

The complete package changes only `main.o` and `xv_d3d.o`; the other 91 objects,
all assets and the updater contract retain identity. Runtime
`b9000d48c5b10f61a83fa0af05138c1edd2cef5781eb05faeb0e2ebc79262829` is installed
and boot-confirmed in slot 1; the sixteen-path runtime remains in slot 0.
Fresh startup confirms `texture-cache 1 depth-prepare 1 available 1` and native
960×544 rendering. Effective clocks remain CPU 444/GPU 222 MHz.

This is the requested retest of compatible options whose prior timing was
mixed or inconclusive. Neither the old timing deltas nor instruction savings
are added together as an FPS estimate. The campaign load took longer in this
observed run, but map/save progress continued and the original checkpoint
loaded successfully; no loading timeout or crash was established.

Ordinary campaign firing, camera/strafe, forward/reverse movement and pause
completed. The final receipt is frame 13,417 with a 1,717,181-byte log, no searched
fault marker and zero logger errors/failed writes. The Vita is paused with
neutral input. Initial sampled active reports are 12.2–12.9 FPS at the same
rounded checkpoint camera, with 142–168 draws and changing AI; no matched
gain/regression or five-FPS improvement is established. Depth preparation
actually skips 1,140 texture preparations per 60-frame report (19 per frame)
in these active windows. Texture reuse is selected, but its separate binding-skip
counter is not enabled. Paused tail reports are excluded from active comparisons.
Blood Gulch combat/driving, rocket pickup/death and prolonged crash qualification
remain outstanding for this combination. All eighteen selections remain enabled.

## Eighteen-path Blood Gulch follow-up

The same boot-confirmed `b9000d48…` process subsequently completed ordinary
Split Screen/New002/Blood Gulch/New003 Slayer, walking and camera movement,
rocket pickup, an explosion ahead, self-hit/death and normal respawn. Screenshots
confirm the equipped rocket, explosion smoke, death/rejoin screen and respawn.
A charged plasma shot consumes 100→89 energy while the Ghost remains visible
before, during and after the charge. This particular view does not contain the
Warthog, so it does not establish both vehicles' visibility during the charge.

Warthog gunner entry/fire/exit and driver entry, steering, forward/reverse,
cliff contact, movement across the open valley and exit/pause also complete.
The open-valley drive changes camera from 90.38,-129.08 to 70.55,-129.35;
this is actual vehicle movement. Different driving/contact windows report
roughly 6–10 FPS; a later stationary view reaches 12.3 FPS. Initial on-foot
views report about 12–13 FPS, with one simpler view near 16. These are different
live workloads, not matched before/after gains or regressions. The combination
has not demonstrated another five FPS or stable 20 FPS.

At the final receipt, frame 46,778, benchmark mode is OFF. The 7,781,736-byte log
contains no searched fault marker and the logger reports zero errors/failed
writes. The Vita is paused on foot with neutral input. All 749 query-prefix
reports in this process remain zero. Short functional success does not settle
the historical intermittent GPU crashes; the session also contains substantial
stationary/menu/paused time and is not a sustained-driving measurement.

Current logs expose different dependencies across views. The campaign
checkpoint's joined object work is about 27.5 ms/frame, while a moved campaign
view waits about 20 ms for exact visibility results without an eligible existing
scene boundary. These are nested elapsed scopes, not additive removable time.
The next diagnostic adds coarse owner tick/scene timing without enabling the
old phase facility that disables object workers. Compatible retained changes
stay cumulative while this larger performance gap is investigated.
