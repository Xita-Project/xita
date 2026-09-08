# Resumed campaign effect coverage — September 6, 2026

The recovered a10 combat checkpoint exposed three VS01 effect pairings that
used the renderer's ordinary texture fallback. A follow-up recording with
camera turns, flashlight toggles and pistol shots exposed an additional VS10
model-lighting pairing. All four definitions are captured from the running
original game; the existing combiner generator produces their fragment sources.

| Vertex program | Captured definition | Canonical key | Required behavior |
| --- | --- | --- | --- |
| VS01 | 1187CB68 | 5F39F5BE | Texture/vertex RGB with inverted vertex alpha |
| VS01 | 3B53556A | D9A5BE50 | Additive RGB attenuation and multistage alpha |
| VS01 | 91A3FFAE | E4CF977C | Additive RGB attenuation and constant/vertex alpha |
| VS10 | FCC09C58 | FCC09C58 | Eight-stage model lighting with cube or 2D reflection |

Five new fragment programs cover these pairings. Previously present fragment
sources remain byte-identical, and native compilation reports five successes
and zero failures. Actual runtime-lookup assertions fail against each preceding
table and pass after adding the corresponding programs. No global combiner,
blending or depth behavior changes.

The shader-package, HUD routing, link/cache and retention tests pass, including
ASan/UBSan, 4,436 C/Python identity checks and 2,036 source-equivalence cases.
The native Vita build passes. Its eighteen-second corridor recording resumes
through the ordinary Campaign menu, then exercises flashlight toggles, camera
turns and two pistol shots. All four captured programs link against the intended
vertex shader. All 62 observed pairings have runtime coverage; two diagnostic
frames check 432 draws with zero captured-data mutations and the log reports no
draw-storage drops. The inspected frames retain the pistol, corridor, corpses
and impact sparks during these actions.

This is coverage of the recorded scene, not proof that every campaign material
or smoke/bloom effect is complete. Hardware rendering and performance remain
pending. The repaired checkpoint and original save backups are preserved; this
shader change does not alter save data or install anything on the Vita.
