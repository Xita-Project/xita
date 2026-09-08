# Avoiding offscreen flare work — September 6, 2026

After restoring immediate flare rendering, one captured cryo frame contained
134 completely offscreen quads among 249 retained VS56 quads. They still paid
for renderer state setup, vertex copies, command recording and GPU submission.

The immediate bridge now evaluates VS56's known affine position transform
before submitting a quad. It rejects a quad only when all four vertices are
outside the same horizontal or vertical clip plane with positive W. It retains
near-plane crossings, nonfinite values, boundary cases and uncertain arithmetic.
A conservative margin accounts for GPU dot-product rounding and cancellation.
Depth visibility remains a GPU query; this check does not guess occlusion.

The common single-quad path rejects before shader/state setup. Mixed batches
keep visible quads in their original order and retain their per-vertex colors.
Empty visibility queries still complete through the existing frame fence with
zero coverage. Core roles are unchanged: this avoids work on the guest and
render threads without moving shared game state to another core.

`XV_FLARE_CULL=0` retains the original path for comparison. Enabling the existing
`XV_WCLAMP` workaround also disables this rejection, preserving that alternate
projection behavior. `[flare-work]` reports kept/rejected quads per 60 frames.

The production bridge test verifies packing and ownership, mixed batches,
skipping state setup, a quad surrounding the viewport, uncertain edges,
nonfinite inputs and nonpositive W. Ten thousand varied affine cases compare
rejections against double-precision clip planes. ASan/UBSan checks pass with
culling enabled, explicitly disabled, and disabled by W-clamping. The native
build passes.

The same cryo view in Vita3K rejects 134 of 249 quads per frame (about 54%).
Its immediate vertex storage falls from 43,760 to 22,320 bytes per frame, saving
21,440 bytes plus the rejected draw setup/submission. This is a work-count
comparison, not a hardware frame-rate measurement. Room geometry, glass and
the technician remain visible. Captures while turning in Blood Gulch retain
the charged plasma orb, vehicles, normal shots, released orb and impacts.
The full turn includes the Ghost and Warthog while the weapon remains charged.
Six sampled geometry checks report zero mutations across 2,281 draws; the run
reports no draw-storage drops. Vita appearance and performance remain pending.
