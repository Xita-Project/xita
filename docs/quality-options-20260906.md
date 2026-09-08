# Graphics and performance options — September 6, 2026

Status: implemented; native build, host, sanitizer and isolated emulator checks
pass. Installed over USB September 6 at 14:00 CDT and verified by direct read
and a fresh read-only mount. All 657 other checked files, including settings and
saves, are unchanged. The device was safely unmounted.

The preceding hardware run recorded a median 9.65 FPS across 38 Blood Gulch
world intervals. Final graphics completion still waited about 59 ms/frame,
despite shorter EndScene stalls. These options make render cost adjustable;
they are not evidence of sustained 20 FPS. See the
[hardware measurement](hardware-20260906-scene-capacity.md).

The [lower-quality hardware follow-up](hardware-20260906-quality-settings.md)
confirms 360p and the new quality settings applied, but records median 8.5 FPS
on a different route. Final graphics-completion wait falls to 43.613 ms while
the engine interval remains 115.8 ms; these timings overlap. CPU 500 MHz was
rejected and fell back to 444 MHz. There is no demonstrated overall FPS gain.

## Dashboard controls

Settings apply when Launch Game starts Halo. Existing settings are preserved;
each dashboard edit saves only the edited key. Restart Xita to change settings
after launching Halo. The dashboard remains at the Vita's native resolution.
The subsequent menu update consolidates all visual options into a scrolling
Graphics list (five visible rows, eleven total); CPU clock stays in Performance.
This menu-only update does not change runtime settings or rendering behavior.

| Page / setting | Choices | Runtime key and default |
|---|---|---|
| Graphics / Resolution | 360p (640×360), 400p (704×400), 480p (848×480), Native (960×544) | `XV_RENDER_HEIGHT=544`; existing 480p selection retained |
| Graphics / Material quality | Low, Medium, Original | `XV_MATERIAL_QUALITY=2` |
| Graphics / Glow | Off, Low, Original | `XV_GLOW_QUALITY=2` |
| Graphics / Particles | Low, Medium, Original | `XV_PARTICLE_QUALITY=2` |
| Graphics / Decal lifetime | Original, 5, 15, 30, 60 seconds | `XV_DECAL_SECONDS=0` |
| Graphics / Decal budget | Original, 32, 64, 128 | `XV_DECAL_LIMIT=0` |
| Graphics / Frame limit | Off, 20, 25, 30 FPS | `XV_FRAME_CAP=0` |
| Performance / CPU clock | 444 or 500 MHz | `XV_CPU_MHZ=444` |
| Graphics / More compressed textures | Off, On | `XV_EXTENDED_BC=0` |

Material Medium removes optional surface detail maps. Low also reduces model
reflections and environment bump/specular/reflection work. Both preserve base
color, alpha cutouts, baked lightmaps, self illumination, and diffuse lighting.
The engine builds materials from modified in-memory tags; map files stay intact.

Glow controls lens-flare layers: Low keeps the central source glow, Off removes
the flare reflections. It does not disable arbitrary full-screen composites,
which are also used by active camouflage and other effects. A separately
isolated full-screen bloom control is not implemented.

Particle quality shortens the lives and fades of named cosmetic smoke, spark,
dust and steam particles to 75% (Medium) or 50% (Low). Particles with collision,
death or material-effect callbacks are excluded, as are projectile, tracer and
energy particles. Projectile damage, physics, AI timing and simulation rate
are unchanged.

Decal lifetime caps temporary impact marks without extending shorter original
lifetimes. Painted signs, water effects and permanent marks are preserved. The
budget expires the oldest temporary impact records through Halo's original
decal updater, which retains responsibility for freeing geometry and lists.

A frame limit only caps frames that finish early; it cannot raise low FPS.
The 500 MHz request requires an installed supporting overclock plugin. A failed
request or a reported clock below 500 triggers a 444 MHz fallback request. Both
requested and reported clocks appear in the log. GPU/bus requests remain 222 MHz
and the GPU crossbar remains 166 MHz; no plugin is installed by this change.

## CPU and GPU implementation

- A native arithmetic block replaces the hot BSP plane-interval calculation at
  `0x88BA5..0x88BF8`. It retains the lifted code's double intermediate arithmetic,
  float spills, x87 state, registers and flags. This is a segment/collision
  traversal helper; it does not move shared game state onto another thread.
- Compressed block reordering can use the existing core-0 texture worker, with
  disjoint output ranges and a join before use. Small work stays local. Existing
  core-0/core-1 geometry sorting and core-1 rendering remain in place.
- Extended compression retains Xbox DXT blocks for rectangular textures and
  complete cube mip chains. It uses GXM block order, mip tails and cube-face
  alignment. Unsupported/incomplete cube chains keep the existing RGBA path.
  The option defaults Off pending comparison on hardware.
- Consecutive color/depth clears merge only within the same target and without
  intervening draws, UI batches or visibility queries. Latest requested color
  and depth values win independently.
- Depth, culling and shader-program bindings are cached within uninterrupted
  mesh replay ranges. Clears invalidate the cache; new ranges after UI and
  scene boundaries start empty. Textures and uniform data remain per draw.

## Validation

- `make -C dashboard host-test`: all new controls, navigation and persistence.
- `python3 tools/test_quality.py`: original settings are byte-identical on
  Blood Gulch, Battle Creek, a10 and UI map tags; lowered settings change only
  permitted visual fields; malformed indices are rejected. Also passes ASan/UBSan.
- `python3 tools/test_bsp_math.py`: 100,000 original/native comparisons covering
  all alignments, split guest pages, floating-point edges, registers, flags and
  x87 state. Also passes ASan/UBSan.
- `make -C recomp/host test-textures test-draw-prep test-frames test`: texture
  cache reuse/invalidation, compressed faces/mips, worker output/lifetime,
  state and clear boundaries, slow-consumer frame ownership and runtime tests.
- Render-target and frame-completion scripts pass. Draw-state and extended
  texture-cache checks also pass ASan/UBSan.

The final native build passes `make RECOMP=1 -j6`. The executable tested in
Vita3K has SHA-256
`bd91e725ebee1b2d4c781c39bdb2bef7223aeb5381536e1a5bf07212cf591414`
for its 32,918,474-byte USB form. The compressed and padded executable segments
match the native SELF; four additional material shader variants are embedded.
No new shader files need to be allocated on the card.

Emulator checks include dashboard pages and saved resolution edits; 360p Low
settings on Boarding Action and Blood Gulch; 400p Medium and Original settings
on Blood Gulch; movement, shooting and leaving through the normal pause menu;
and the final 360p Low build through a10's opening cinematic into the cryo room.
Original material quality restores terrain detail. Low/Medium remove substantial
terrain detail and are optional visual tradeoffs, not new defaults.

The live decal pool contained exactly 32 temporary marks and the same 8 permanent
map decals after sustained firing. The original updater freed expired marks.
The final Medium, Original and Low sessions report zero missing shader pairs,
texture initialization failures and geometry-buffer drops. The new reduced
varying shader is confirmed linked against `halo_vs_27` in a10.

Vita3K uses a software OpenGL renderer in an isolated display, not a Vita
performance model. Existing `v_Color0` link errors and unsupported `0x30000000`
color-format messages also occur in the preceding build. Eligible compressed
cube layout is covered by synthetic full-chain tests; no eligible compressed
cube upload was observed in these map runs. Hardware FPS and appearance still
need the next device run.

Build, source snapshot, validation logs, screenshots and rollback executable:
`/home/birchwoodgod/xita-backups/2026-09-06-133940-quality-options`.
The VPK is `xita-quality-options.vpk` in that directory.

## Hardware hand-off

The existing 480p/128-texture settings remain selected on the device. New quality
controls start at Original, frame/decal caps and extended compression start Off,
and CPU remains at 444 MHz until changed in the dashboard. Actual hardware FPS
and appearance for the new options are unmeasured.

## References

Tag fields are checked against the locally owned Xbox maps and the
[Invader HEK definitions](https://github.com/SnowyMouse/invader/tree/master/src/tag/hek/definition).
GXM compressed layouts and cube alignment are cross-checked against Vita3K's
[texture format conversion](https://github.com/Vita3K/Vita3K/blob/master/vita3k/renderer/src/texture/format.cpp)
and [texture cache](https://github.com/Vita3K/Vita3K/blob/master/vita3k/renderer/src/texture/cache.cpp).
The optional CPU request follows the behavior of
[PSVshell's clock hooks](https://github.com/Electry/PSVshell/blob/master/src/main.c).
