# Fog interfaces and full-screen effects — September 8

This separate rendering candidate repairs shader-interface mismatches found while
validating the CPU experiments. **It is not installed on the Vita.** The installed
weapon/menu/audio and visibility build remains the next hardware baseline.

## What failed

The Vita3K backend repeatedly rejected fragment programs requiring `TEXCOORD7`
or `COLOR0` when the paired vertex program did not emit those outputs. The errors
also occur in the earlier baseline. Game-side GXM success checks did not report
these backend link failures, so absence of draw-storage drops was insufficient
proof that every emulator pass rendered.

Nine generated vertex sources still declared the fog output as `FOG`, although
`shader_recomp_gen.py` and the generated fragment programs already used
`TEXCOORD7`. Their compiled GXP metadata confirmed the old output. The affected
programs are VS05, 08, 09, 14, 18, 20, 23, 41 and 54. VS41 paired with
`ps_D05FE9D0_7C` was one concrete mismatch in the runtime trace.

The remaining color errors came from full-screen VS38 draws entering the legacy
UI path. That path selects one texture and a color-multiplying fragment, while
VS38 emits four texture coordinates and no color varying. Captured programs
`423B087D`, `4AB2DD7B`, `BDC872E6` and `9D4F5445` share one canonical four-texture
combiner (`4ECE4699`). A loading variant, `B8A3D35F` (`7377025A`), also uses a
dynamic combiner constant. Neither canonical program was covered by the previous
composite routing table. Later in the opening cinematic, the eight-stage
`1F88B236` composite exposed the same gap. Its complete active program is now
captured and routed as well.

## Changes

- Rebuild the nine vertex programs with the matching fog semantic. Position,
  matrix, UV and fog arithmetic in their sources is unchanged.
- Compile the three captured composite programs, including their alpha-disabled
  variants, using the installed stage's full-precision generator. Match their
  exact canonical identities and route them through the existing mesh recorder.
  The already compiled `8C25D9E4` composite now follows this route too. All four
  textures, constants, alpha/blend state, order and per-stage UV conversion use
  the established composite path. Other UI programs retain their prior routing.
- Embed all 67 layout-referenced vertex programs in the executable, alongside
  the existing embedded fragments. An executable-only USB update can therefore
  carry matching layouts and both shader stages even when older shader files
  remain on the card. The explicit development override retains its precedence.
- Check compiled vertex output metadata against each generated source while
  preparing the embedded header. This catches the stale fog interface before
  packaging; it does not prove arithmetic equivalence of arbitrary shader edits.
- Ignore and exclude the new generated header from public-release review exports.
  Captured definitions and translated shaders remain private inputs.

The metadata check reads the documented structure in the
[Vita3K GXP types](https://github.com/Vita3K/Vita3K/blob/master/vita3k/gxm/include/gxm/types.h)
and its [output interpretation](https://github.com/Vita3K/Vita3K/blob/master/vita3k/gxm/src/gxp.cpp).
It does not execute or disassemble guest shader instructions.

## Validation and limits

All 67 compiled interfaces match their sources, including the nine corrected fog
outputs. The same audit rejects the installed pre-fix shader set. Host tests
exercise composite recognition, dynamic constants, rejection of active-state
changes, mixed linear/normalized UV rows, embedded program selection with missing
or stale package files, development overrides, and shader-cache lifetimes.
Canonical identity and source-equivalence checks pass, as do ASan/UBSan
checks of all six recognized composite programs. The six newly compiled
fragment variants consume exactly TEXCOORD0–3, with no color varying. Native builds pass in an
isolated stage based on the installed source, with both CPU experiments excluded.
Release-export tests preserve originals and exclude the new embedded header.

Private emulator checks and final hashes are recorded with the candidate in
`xita-backups/2026-09-08-065547-skate3-vita-optimization/`. The final executable-only run keeps the older packaged shader files in place.
It reaches the Keyes cinematic, links the previously failing VS41/fog pair and
the captured eight-stage VS38 composite, skips into cryo gameplay, and accepts
further camera turns and flashlight input without recurring varying-link failures. Exactly three obsolete pairs fail during startup cache loading; the
shared cache is preserved. Runtime draw failures are distinguished from those
startup attempts. The preceding rendering run covers ordinary solo launch,
Blood Gulch movement/firing, flashlight input, grenade damage/deaths/respawns,
Pause / Leave Game and campaign loading.

A broader metadata audit finds no incompatible varyings in **1,206 table shader
pairs**, including normal and alpha-disabled fragments. This excludes the legacy
UI fallback programs and does not establish coverage for uncaptured effects.

The opening also uses a RGB565 render target. Vita3K's OpenGL
[color-format conversion](https://github.com/Vita3K/Vita3K/blob/master/vita3k/renderer/src/gl/color_formats.cpp)
lacks `SCE_GXM_COLOR_BASE_FORMAT_U5U6U5` and logs unknown format `0x30000000`,
falling back to RGBA8. That format exists in the installed VitaSDK and Xita passes
separate color/texture enums correctly. The native target format is preserved;
this emulator limitation prevents claiming exact cinematic color fidelity.

This corrects missing rendering work. It is not a demonstrated hardware FPS gain
or proof that the prior GPU crashes are fixed. Validate fog, full-screen effects,
loading, camera movement, firing, vehicles and death on Vita before accepting it.
Keep the installed visibility comparison separate from this rendering change.

Native SELF: 34,978,954 bytes, SHA-256:

```
debb442c3d12a62dac4f0118f0080dfb80a2b4cdba194c50d46391e68ab63f39
```

Source: `varying-stage/`; 5,269-file manifest: `varying-stage-manifest.json`.
Final cinematic validation: `varying-cinematic-run/`. The preceding
`varying-final-run/` covers Blood Gulch, loading, grenade deaths/respawns and
campaign entry, and exposed the final eight-stage program. Its executable and
logs are retained separately. Neither rendering build includes the CPU candidates.

The final emulator is stopped and its previous executable and configuration are
restored. 3 ownership samples cover **1,248 draws**, with zero changed
geometry bytes before completion. Complete game-log reports show no upload failures, rejected constants, fence
errors, draw-storage drops or ordinary Finish calls. One final timing row was
truncated during deliberate emulator shutdown and is excluded from that count. Peak upload
use is 1,983/8,192 KiB per slot; maximum pending GPU packets is one.
The RGB565 backend messages remain a separate limitation.
