# Independent visual switches

Four controls are added to **Graphics** in both the dashboard and the in-game
Select + Circle panel. All default to **On**, preserving the existing path.
Changes are saved immediately and take effect after **relaunching Xita**.
Map files, saves, collision and simulation timing are not rewritten.

| Control | Setting | Off behavior |
|---|---|---|
| Temporary decals | `XV_TEMP_DECALS=0` | Remove effect emitters for finite-lived impact decals; preserve permanent marks, water-type decals and map decoration. |
| Cosmetic effects | `XV_COSMETIC_EFFECTS=0` | Remove selected smoke/spark/dust/steam particle emitters before allocation. Preserve projectile/tracer/energy particles and particles with collision/death/material callbacks. |
| Material reflections | `XV_REFLECTIONS=0` | Remove optional environment/model reflection maps and their reflection brightness. Keep detail maps and ordinary lighting at the selected material quality. |
| Object shadows | `XV_OBJECT_SHADOWS=0` | Set the engine's object-tag no-shadow flag. Baked world lighting remains unchanged. |

These are independent controls, not another preset. Existing Low material
quality still removes reflections. Turning the new reflection switch On allows
the selected material quality's behavior; choose Original to restore full
reflections. Likewise, the older particle detail and decal lifetime/budget
settings retain their existing meanings. Their zero values still mean Original,
not Off. Unsupported/invalid new values fall back to On.

The cosmetic control deliberately does not disable all effects. Damage, sound,
lights, projectiles, event timing and remaining emitter order are retained.
Particle systems and unknown particle types keep their original behavior.
The decal switch filters selected effect parts at map load; it does not add a
per-frame pool scan or directly free guest allocations. The existing optional
decal budget still uses the original engine updater.

Object shadows are a tag-level request to Halo's shadow renderer, not a blanket
skip of render targets. Material reflections refer to environment/model maps;
this does not disable arbitrary transparent glass or water passes.

**Bloom remains a separate follow-up.** The existing Glow / lens flares control
can be disabled, but it is not a full-screen bloom switch. Shared composites also
serve active camouflage and other effects; identify the dedicated producer and
consumer before disabling a pass.

## Validation

`make -C dashboard host-test graphics-test` covers navigation, scrolling, each
new setting, persistence, restart labels and preservation of unrelated settings.
The overlay test includes AddressSanitizer and UndefinedBehaviorSanitizer.

`tools/test_quality_switches.py` exercises the production override code against
synthetic protected emitters, malformed effect arrays, mixed damage/sound/decal
events, invalid settings and optional locally owned maps. Each independent
switch and all four together are compared byte-for-byte with a tag-level
reference. Defaults leave all bytes unchanged. Sanitizer checks pass on Blood
Gulch, Battle Creek, a10 and UI. Existing quality tests pass on the same maps.
Physical appearance, stability and performance are recorded separately below
when measured; these tests alone do not establish an FPS improvement.

Field layouts were cross-checked against the original Xbox caches and Invader's
[effect definitions](https://github.com/SnowyMouse/invader/blob/master/src/tag/hek/definition/effect.json)
and [object definitions](https://github.com/SnowyMouse/invader/blob/master/src/tag/hek/definition/object.json).
The implementation is handwritten; no game data is included in these tests.

## Physical Vita smoke test

The private update installed successfully through the existing Wi-Fi updater;
runtime SHA-256 is
`f3144c2cbc8372a1d0a382bde858cdad2db72ffe2980b778a5d9e8c080ef3df0`.
The package has the same 1,588 members and asset contract as its predecessor;
only `game-a.self` and `boot-game.txt` differ. The preserved A executable remains
available. Use the executable hash to identify this incremental build: the
remote status date comes from an unchanged translation unit and still displays
September 16 01:33:17.

All four switches are visible in both physical dashboard and in-game panel;
saved Off values are confirmed by the startup/map logs. Blood Gulch reports:

- 214 temporary-decal emitter records removed.
- 393 selected cosmetic-particle emitter records removed.
- 120 environment/model material tags adjusted at otherwise Original material
  quality, and 70 object tags given the no-shadow flag.

These are **map-definition counts**, not particles avoided per frame. Normal and
charged plasma firing, camera/movement input and grenade input were exercised.
The world, weapon and HUD remain visible, the device continues rendering, and
the captured run contains no worker STOP, missing-program report or GPU fault.
This is a short smoke test, not broad campaign or crash-stability certification.
Shadow absence and all transparent materials still need wider visual coverage.

The user's saved resolution was already **640×360** before this update; earlier
overlay edits are recorded in the preserved old log. It was retained. Settled
203-draw/19-BSP windows in the tested valley view report **8.7–9.2 FPS**, median
9.0 across seven reports. Later movement/firing windows report 8.7–9.2 FPS as
well. Looking down subsequently shows roughly 28 FPS in a screenshot, which is
not a representative gameplay result.

There is **no matched-camera On/Off performance comparison** for these controls
yet. Do not compare this view directly with earlier native-resolution captures
or attribute a gain/regression to the switches. The controls demonstrably remove
selected work but have not established a sustained FPS improvement.

The four switches are left Off for this private hardware trial; existing
resolution and other settings are retained. They default On for other users.
The next attribution work is the [rendering checklist](render-bottleneck-plan-20260916.md),
especially attachment traffic and compatible fragment simplification, alongside
native CPU preparation. Representative 20/30 FPS goals remain open.

Private logs, package hashes, screenshots and analysis are under
`xita-backups/2026-09-13-worker-sizing/validation/engine-restructure-20260914T2300Z/physical-visual-switches/`.

## Longer user gameplay

The user reports about 20 FPS in Blood Gulch's caves and around its bases,
about 10 FPS looking down the valley, and later 20 FPS in Battle Creek. These
are location-dependent observations, not paired measurements of the new switches.
The read-only log snapshot `user-gameplay-20260916T173413Z` confirms the same
runtime hash, startup 360p/all-four-Off settings and multiple 20–22 FPS windows.
No input, setting change or benchmark was sent while collecting it.

The snapshot contains 46 world-rendering report windows. Its initial eleven
windows face downward at an unchanged camera and reach 25.7–29.3 FPS; do not use
the whole-tail median as a representative gameplay average. Two later examples
illustrate why CPU and GPU work both remain investigation targets:

| Report | FPS | HLE draws/frame | Draw-HLE time | Completion latency |
| --- | ---: | ---: | ---: | ---: |
| Faster moving-view window | 20.4 | 107 | 6.9 ms | 38.463 ms |
| Slower moving-view window | 13.4 | 149 | 9.0 ms | 38.649 ms |

These are different cameras and actions. The completion interval overlaps CPU
work and is not GPU active time; subtracting it from total frame time is invalid.
The similar completion measurements alongside different frame rates rule out
treating resolution-sensitive GPU work as the only cost in every view.

The longer capture also contains six bounded missing-program warnings for
vertex `9E2E9021` / pixel `A2E9FFCD`, which were absent from the short smoke test.
The exact 240-byte definition is preserved privately and its hash validates.
The program is absent from the installed table; provisional cube and 2D Cg
variants generate without warnings, but have not been compiled, installed or
validated visually. The nearest existing definition differs in an alpha-stage
operation; no unproved shader alias was installed. This remains a rendering
follow-up and limits claims of complete visual correctness.

The full pre-update log also confirms loading `beavercreek.map`, followed by
66 world-rendering report windows, 30 at or above 20 FPS. The window median is
19.5 FPS, with a 6.3–27.4 range including load/transition effects. This supports
the user's Battle Creek observation but is not sustained 20 FPS or a controlled
gain measurement. Menu/loading-only reports are excluded from those 66 windows.
The archive is `depth-store-validation/install/before-0.log`; the extracted
rows and map markers are in `depth-store-validation/battle-creek-prior-run.json`.
