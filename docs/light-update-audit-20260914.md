# Locating the light-update work

The earlier physical object/scene profiles placed `0x92330` among the costly
selected routines, at roughly 4–5 ms/frame in the stationary Blood Gulch
captures. Selected self includes uninstrumented descendants. This is a useful
profiling target, not evidence that the function's own arithmetic costs that
much in every scene.

Inspection of the matching owned Halo CE 3925 executable identifies its table:
`0x92780` initializes `0x2FC67C` with a capacity of `0x380`, record size `0x7C`,
and the original name `lights`. It registers the related structure at
`0x2FC670` using the name `light`. `0x92330` operates on entries from this table.
The supported image SHA-256 is
`4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae`.

The routine has 194 unique original instructions in the inspected body. It
reads object transforms and light-tag parameters, updates position/direction
fields, computes spatial bounds and calls `0x56670`. The latter writes shared
query state at `0x2D2FAC`/`0x2D2FA9`, obtains entries through `0xA9330`, and links
records through the structure passed in `edi`. These shared mutations prevent
assuming that complete light updates are independent worker jobs.

A candidate boundary would gather transform/tag inputs, compute independent
light geometry, then retain the ordered shared-structure updates on the owner.
The next measurement must separate the transform/normalization, spatial query
and shared-update children before choosing that boundary. Cached spatial-query
results would additionally need a proven complete dependency set and map
lifetime; an unchanged light address alone would not justify reuse.

The user's report that additional visible NPCs reduce FPS also warrants a
character-facing/away comparison of animation, model-lighting and draw-preparation
cost. That observation does not identify AI, physics, or the GPU as the sole
cause. Camera changes also change world geometry and effects, so those views
need screenshots and separate counters rather than an NPC-only speedup claim.

No light behavior or rendering is changed by this audit. Raw executable data,
generated code and the private audit receipt remain outside Git.
