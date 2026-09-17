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
