# Blood Gulch rendering work

Blood Gulch is the primary rendering and performance test map at the user's
September 5 request. Resolve shared rendering problems here, then revisit the
campaign cutscene freeze, camera transitions, AI and checkpoint resume.
The user revised the first playable target to sustained 20 fps on real Vita hardware,
with 25 fps as the next milestone. Consistent 50 ms frames and stability come first.

September 8: the [weapon stencil correction](weapon-menu-20260908.md) keeps the
rifle intact against close walls in Vita3K. Solo lobby fixes pass emulator checks.
These local changes have not been installed or verified on hardware.

The [September 6 overnight report](overnight-progress-20260906.md) records the
latest local fixes and hardware checks still needed. Its Blood Gulch recordings
show normal plasma bolts, charged projectiles and impacts, and a charged camera
turn keeps the Ghost and Warthog visible. Historical investigations below retain
their original observations; they do not describe the latest candidate's status.

## Current order

First resolve the integrated build's [hardware crash](hardware-20260905-bloodgulch-crash.md)
so each rendering case can be tested without the frame-buffer reuse race.

| Priority | Case | Repeatable check |
| --- | --- | --- |
| 1 | White/gray sky haze, missing sky and dark sun/disc | Hold the same view, then compare sky toward the ring, sun and horizon before/after the trigger. |
| 2 | Geometry spikes and disappearing surfaces | Walk the same base-to-canyon route and turn through the same views; compare emulator and Vita captures. |
| 3 | Flashlight, plasma, smoke, bloom and active camouflage | Compare effects off/on from a fixed view; inspect projectiles and the camouflage transition separately. |
| 4 | HUD and first-person weapons | Check health/shield changes, reticles, sniper zoom transitions, arms and nearby-wall depth. |
| 5 | Performance of corrected rendering | Repeat the route with the same settings; report frame-time stalls and slow windows as well as average fps. |

Performance is measured throughout this sequence, not deferred until every visual
bug is resolved. Emulator timing does not establish Vita performance. Keep 480p,
texture size, filtering and mip smoothing fixed within each comparison; test a
settings change separately. Campaign-only failures remain on the roadmap.

The user specifically reports that plasma-pistol projectile effects remain
missing. A visible weapon charging/firing glow does not verify the bolt itself;
capture normal shots, charged shots, trails and wall impacts as distinct cases.

The user clarified that active camouflage turns their own entire character
purple, including first-person arms/weapon. Other players are not confirmed
affected. Capture the same weapon/arms and their character in third person before
pickup, during camouflage and
after expiration. Inspect the camouflage material, sampled render target and
blend state before choosing a correction; the color alone does not establish a
missing texture or a shared cause with the sky haze.

## Local follow-up, September 5 evening

The user confirms the sky looks fixed, impact decals leave marks again, and
active camo works in the latest local emulator candidate. These are emulator
observations; no hardware deployment or hardware FPS measurement occurred in
this work. See [implementation and validation](rendering-20260905-local.md).

The user requested a 20 fps emulator cap and finds it playable if geometry is
stable. `XV_FRAME_CAP=20` paces presentation every 50 ms while retaining the
real-time guest clock. Gameplay windows repeatedly log 20.0 fps. The cap is set
in the isolated emulator config, not packaged as a hardware default.

Turning left/right reproduces spikes independently of firing or reloading.
Eighty screenshots during a camera spin show stretched/missing canyon walls,
ground and base triangles while the weapon stays intact. A subsequent diagnostic
compared draw data from recording to GPU completion: vertex data stayed stable,
but world index lists changed (47 affected draws in one traced frame).
The candidate now copies guest indices into a bounded frame-owned pool; its host
regression reproduces source overwrites and verifies retained topology.
The user confirms the camera-turn spikes look fixed; a follow-up trace checked
1,011 draws over 12 frames with zero mutations. In the subsequent Vita run,
spikes initially seemed gone in multiplayer/campaign but later recurred less often.
The index-lifetime fix is retained; hardware geometry stability is still open.
The user subsequently reported angle-dependent model pop-in: a Warthog body was
missing, looked correct after restarting, and rocks disappear/reappear with camera
direction. These remain unresolved; captures and traces are retained.

## White haze capture, September 5 at 18:07 CDT

The live Vita3K view shows a gray sky with visible stars and the Halo ring. The
agent captured the view and requested frame 6401's draw trace without sending
controller input. The user subsequently confirmed that looking at the sky from
certain directions triggers the haze. Record the exact view direction and compare
adjacent clear/hazy views. This is the same executable just deployed to the Vita:
`caedaae7c61396c402e5dbc70c833474f7685b1f925230f58e560ee5259bbd1d`.
Settings are 848x480, medium/256 textures, linear filtering and mip smoothing on.

The trace starts with a tan clear (`00FFE6C4`), then five depth-disabled sky draws
using VS47 (`AC1984DB`). Their combiners are `497E6718`, `70E4887F` and `6776AFE7`.
All three successfully linked before the capture, so this occurrence is not the
previous twelve-links-per-VS cache exhaustion.

The first sky combiner computes texture 0 times texture 1, adds texture 2, then
applies its constant-color mix. Stage 2 has a bound 256x256 format-06 texture, but
the recorded GXM texture is the bridge's 16x16 gray cube fallback. The recording
code replaces a successfully decoded non-cube texture when the compiled shader
expects a cube sampler. Adding that gray sample is a specific haze hypothesis;
the correct addressing of the original texture still needs verification.

For comparison, xemu selects its sampler from the bound texture type and handles
a cube-mode lookup on a 2D texture through an explicit direction-to-UV mapping:
[xemu pixel-shader implementation](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/glsl/psh.c).
The next investigation should capture the original header/type and texels and
validate this path before implementing a shader specialization. A global change
to fog or fallback color would not establish correct sky rendering.

Capture evidence is under
`/home/birchwoodgod/xita-backups/2026-09-05-181109-bloodgulch-haze/`;
`/tmp/xita-bloodgulch-haze-path` records the actual directory for this session.
This entry records a reproducible failure and a concrete lead, not a completed fix.

## Shader coverage from the crash-corrected emulator session

The final 18:20–18:30 Blood Gulch log reports 250 unique vertex/combiner pairs;
97 have no exact runtime-table entry. Comparing inactive-stage-normalized
definitions, 65 of those pairs have an already compiled equivalent on the same
vertex shader. The current parser/generator produces byte-identical source for
each of those 65 comparisons with that vertex shader's actual outputs. The
remaining 32 need further analysis. This provides a concrete path to reduce
incorrect material fallbacks without changing shader behavior or requiring new
compiled programs for those aliases. It does not identify which pair caused camo
or the missing projectiles; do not label either fixed from this comparison.

The comparison uses the normalization from the previously reviewed `293fa02`
branch (inactive combiner fields/mapping nibbles and constant colors); no hash
migration or alias correction has been applied to the crash recovery build.
The runtime's draw-pair log caps at 256 entries, so these are observed coverage
figures, not complete shader coverage. The captured log and
`effects-shader-coverage.json` are in the crash report's `recovery/` directory.
