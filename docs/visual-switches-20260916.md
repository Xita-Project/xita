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
