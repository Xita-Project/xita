# Earlier depth-only preparation — September 15

**Physical follow-up:** one native-resolution cryobay comparison completed with
matching camera checks and restored state. It measured 5.764 / 5.753 / 5.718 FPS
off/on/off. The on arm falls between the off arms; a pooled 0.373 ms/frame
difference does not establish a repeatable gain. This option remains off.
See the [render-preparation report](hardware-render-preparation-20260915.md).

The [query audit](query-submission-audit-20260915.md) found 119 draws in a cryo-room
capture that prepared texture stage 0 before replay selected a constant fragment
program with no samplers. The new optional path eliminates that preparation and
the repeated original-fragment lookup for already verified combinations.

The render thread publishes a proof only after the original alpha-disabled
fragment links successfully, has no discard or depth replacement, and the
constant depth-only fragment also links. The recorder can then select that
constant fragment before preparing textures. The cache key includes the vertex
layout slot, canonical pixel-program entry and blend variant. Each key is
published with release/acquire operations; a hash collision causes a cache miss.
Already published immutable keys do not issue another release barrier per draw.
No recorder access to the render thread's mutable link-cache structures is needed.

Only color-masked draws with disabled or ALWAYS alpha tests qualify. Programs
whose texture kind can select another shader variant retain ordinary preparation.
Failed links, heuristic fallbacks, active alpha tests, coverage-changing shaders
and development shader overrides do not establish usable proofs. The actual
linked alpha mode is checked because a failed alpha-disabled link can fall back
to the original variant.

Each command captures its decision. Disabling the option after recording cannot
make that queued draw sample textures it did not retain. Vertex constants,
geometry snapshots, depth/stencil state, query IDs and ordered draw submission
stay on their existing paths. Linked programs remain allocated until drained
shutdown; proof storage is cleared at that shutdown. No GPU wait is added.

## Testing and measurement

`XV_DEPTH_PREPARE=1` enables the experiment. Its default is off pending physical
frame-time and gameplay validation. The internal remote benchmark kind
`depth-prepare` runs off/on/off at the current resolution and restores the
configured setting on completion, cancellation or loss of first-person control.
It retains the other graphics and worker settings. The `[depth-prepare]` log
reports how many draws actually omitted texture preparation; an on arm with no
hits cannot demonstrate this optimization.

Host tests extract the production recording and replay decisions. They cover
delayed cross-thread publication, collisions, changes in layout/program/blend,
failed original and constant links, alpha/cube/depth/discard guards, incompatible
settings, and queued commands surviving subsequent option changes. ASan/UBSan
pass. The production benchmark controller covers all three arms and restoration;
the actual Present dispatcher changes only the selected optimization. HTTP tests
cover the new benchmark selector and existing authentication/admission behavior.

The built-in depth program's compiled and embedded bytes still match; it has no
varyings, samplers, uniforms, discard, depth replacement or buffer stores. No
shader bytecode or translated game function was changed for this candidate.

The final native candidate (`2ff6f2eb…`, full runtime SHA-256 below) boots through
the ordinary menu into campaign in the private emulator. One off/on/off capture
retained the same 121 ordered query IDs in each arm, with the same color mask,
alpha mode and constant zero-sampler fragment. Texture preparation per query
changed from one stage to zero, then back to one. Across the three traced frames,
all 1,480 retained draws were unchanged between recording and GPU completion.
The configured enabled setting was restored afterward. Live simulation continued,
so total scene draw counts differed between those frames.

This was a diagnostic run: the emulator was capped at 20 FPS and per-query logs
and three frame histograms were enabled. The game's controller completed all
three arms; the extra logs exceeded the client's bounded benchmark-result tail
collection. Its failed client receipt is preserved, and a separate pinned full
log capture confirms completion and restoration. No timing improvement is claimed.

Runtime SHA-256:
`2ff6f2eb0b7932d6644698f85c30af543497b422632fbc090cad7a7294bddf91`.
The package still passes the existing updater contract; only the game executable
and boot selector differ from the comparison package.

The same candidate loads Blood Gulch through the ordinary split-screen menu and
continues rendering after two charged plasma shots, with impacts visible. One
outdoor view reports only two eligible draws per frame, versus 121 in the campaign
comparison. The amount of work removed is scene-dependent; this is not evidence
of a solution to the broader Blood Gulch driving bottleneck.

Physical hardware remains unreachable through its remote service. Emulator
checks validate integration and work removal, not Vita FPS or the unresolved
physical GPU crash. Standard-settings Blood Gulch driving and campaign combat
remain required before enabling this path by default.

The local Vita3K log explicitly marks the visibility-buffer call as a stub that
sets every query visible; its visibility enable/index/operation calls are also
unimplemented. The captured result words consequently saturate to `UINT32_MAX`.
Matching emulator query counts cannot validate physical occlusion coverage.
Use this instance to check draw identity, shader selection, retained geometry
and control transitions, then verify actual query results on hardware.
