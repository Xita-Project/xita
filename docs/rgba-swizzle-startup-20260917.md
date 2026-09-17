# Cumulative RGBA layout candidate — September 17, 2026

The retained decoded-RGBA swizzle path can now be selected at process startup
with `XV_RGBA_SWIZZLED_DEFAULT=1`. The repository default stays zero; an explicit
`XV_RGBA_SWIZZLED` environment selection still takes precedence. Incremental
builds track this default and rebuild only the uploader object when it changes.
No benchmark action or runtime toggle is needed to activate this candidate.

This adds a ninth path to the eight-path cumulative gameplay build. It retains
the collision helpers, full query fusion, grouped vertex comparisons, earlier
query publication and conditional depth-store omission. Texture resolution,
mips, pixels, sampling and transparency remain unchanged. Only eligible decoded
power-of-two textures change storage layout. Compressed textures, cube maps,
render-target aliases and unsupported dimensions keep their existing paths.
Scratch allocation failure retains a cached linear upload; startup ON does not
mean every texture was swizzled. Bounded existing upload logs now report the
actual GXM descriptor layout, separately from the startup selection.

## Validation

The actual production texture upload/cache suite passes normally and under
ASan/UBSan: 164 exact multi-mip upload cases, larger Morton-layout cases, cache
reuse and invalidation, immutable old uploads, opacity, allocation failure and
layout exclusions. An independent review passes 15 fresh-process startup cases
covering absent/default-zero/default-one builds and absent/zero/one/empty/invalid
environment selections. These check actual uploaded pixels and layout, not only
configuration return values. Six incremental Make transitions verify selection
and rebuild isolation; invalid build defaults are rejected.

The retained Vita stage builds successfully. Only `xv_ui_gxm.o` changes among
object files. Three rebuilt archives differ in headers only; member names,
order and payloads match exactly. All packaged assets and the updater contract
match the eight-path parent. Only the runtime SELF and its boot hash change.

Candidate runtime SHA-256:
`d817f077ef551ae1da2888f895497be711d03a51c60d7921048486869d68c668`.
VPK SHA-256:
`493d906732971d7de32c875f5f1cb684b1a2b62ea04cbf8610b3f8fe3a058ff5`.
Private build, test and package evidence is in
`direct-cluster-query/rgba-swizzle-startup/`.

The updater has installed and boot-confirmed this runtime in slot 1. The
eight-path `98393693…` parent remains in slot 0 for rollback. Physical startup
logs confirm the selected mode and actual decoded RGBA swizzled descriptors,
with the expected non-power-of-two linear upload. The main menu renders.
New001's same Normal campaign checkpoint loads in this fresh process and reports
about 12.8 FPS after initial texture loading. Two fire inputs, camera/strafe and
pause complete with no searched fault marker. Captured checkpoint and NPC views
render correctly; this is limited smoke evidence, not a claim that all rendering
or long-session crashes are resolved. No repeatable gain or regression is
established; all nine paths remain enabled. Blood Gulch follow-up is in progress.
Earlier
[layout validation](rgba-swizzle-20260907.md) did not establish a physical Vita
performance result. No FPS gain is claimed from host correctness or instruction
counts. Retain compatible changes together unless a regression is established;
use ordinary fresh-launch gameplay with standard graphics for this evaluation.
