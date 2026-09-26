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
