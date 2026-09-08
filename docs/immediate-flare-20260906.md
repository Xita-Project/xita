# Immediate flare draws — September 6, 2026

The immediate bridge previously accepted only the screen-space UI declaration.
Blood Gulch also emits quads with the particle declaration `001E13BC` and VS56
(`405809D3`). Captures show visibility-test rectangles and textured flare
sprites. All of these were discarded before draw recording.

The bridge now records the observed VS56 format with a dedicated layout:
position XYZ, UV, packed diffuse color, and per-vertex v10. Keeping v10 in the
vertex stream preserves changes within a quad instead of substituting its last
persistent value. The ordinary streamed VS56 layout remains registered first
for hash lookup. The new 40-byte vertices use the existing frame-owned immediate
pool and fences.

Two captured fragment definitions are embedded: `28CF808C` for the visibility
rectangle and `4CD25D32` for the textured flare. Original depth, color masks,
constants and blending are retained. Temporary immediate-draw traces have been
removed from the implementation.

Validation: the production packing bridge passes ASan/UBSan checks with the
captured position/depth/color, changing v10 per vertex, 64+4 vertex batching,
source overwrite, and preservation of the streamed layout. Shader/cache tests,
4,400 identity checks, 2,021 source-equivalence checks, native compilation of
both fragments and the Vita build pass. Vita3K now records and links the
previously discarded programs; inspected Blood Gulch charge frames retain the
world and corrected radar.

The plasma charge glow still needs work. `GetVisibilityTestResult` currently
returns one visible pixel for every query, while Begin/End are no-ops. This is
a separate limitation for Halo's flare intensity and occlusion. The Vita SDK
exposes visibility buffers/tests; query completion and guest polling must be
handled without introducing a same-frame deadlock. Hardware validation is pending.

Follow-up: [GPU visibility queries](visibility-queries-20260906.md) replace the
one-pixel stub and restore the idle/charged plasma glows in Vita3K. Hardware
validation remains pending.
