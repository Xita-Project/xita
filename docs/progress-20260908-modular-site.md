# September 8: modular foundation, backups and website

## GitHub and preservation

The user authorized private GitHub updates and a separate private website
repository. The first source backup was pushed and read back as commit
`39de2bfda07eace4ce52dc4640d50a9b753efef7` on
[`work/2026-09-08-progress`](https://github.com/BirchWoodGod/xita/tree/work/2026-09-08-progress).
The validated follow-up updates that branch and `main`. Original local work,
game inputs and private diagnostic/build stages remain preserved. No history
rewrite or repository visibility change is part of this update.

## Implemented and checked

- [Modular profiles and archives](modular-architecture.md): 28 Halo overrides
  moved into its revision-pinned profile, hooks moved out of the lifter,
  shared XDK policy extracted, and native libraries split by ownership.
- Ten synthetic profile tests cover actual x86 lifting, independent HLE choices,
  revision/manifest validation, output ownership, stale chunks and failed hooks.
  Existing math/quality/flare and native clipper checks also pass.
- Full traced Halo regeneration preserves all 35 generated C/header files
  byte-for-byte. The separate Vita build succeeds, with all 8,595 inspected
  guest/HLE/kernel/native-helper symbol bindings and sizes unchanged.
- [VPK installation](installing.md) is now the primary setup path. The experimental
  native Windows build guide was removed after review; WSL source-building notes
  remain available for developers. One-time game data preparation is documented
  separately from building the app.
- Python VPK packaging avoids an argument for every shader. Required-input,
  exclusion and ZIP tests pass. The development packages are also compared with
  the official VitaSDK packer's file payloads.
- Source-export rules now retain game profile JSON, runtime Make fragments and
  original `.inc` files, while excluding the generated native game clipper.

## Separate website

The new [xita-website repository](https://github.com/BirchWoodGod/xita-website)
is private. It contains the **xita.dev** project/progress page, dated development
updates, hardware measurements, labeled screenshots, roadmap, architecture and
FAQ. The old `site/` directory is retained as a historical draft.

Browser checks pass at 320, 390, 768 and 1440 pixels: no horizontal overflow,
working gallery filters, image dialog, Escape/focus restoration, FAQ and
JavaScript-disabled fallback. No external fonts or analytics are required.
No website deployment, DNS change or GitHub Pages activation was performed.

## Private VPK recovery and candidate

The [private development release](https://github.com/BirchWoodGod/xita/releases)
contains two clearly separated packages, checksums and build information:

| Package | Meaning |
| --- | --- |
| `xita-installed-20260908.vpk` | Recovery package with the **exact executable** USB-installed and readback-verified at 07:00 CDT. |
| `xita-local-rendering-modular-20260908.vpk` | Local shader-interface/composite corrections plus the library split. Local validation only; not installed on physical hardware. |

Both packages require the user's existing game image/maps. They contain no
saves/settings or SDK/firmware module. They **do** contain translated game code
and shaders and must remain private; release-asset review is required before any
future public repository/source release. Their tag identifies the packaging
and documentation snapshot; build information separately identifies the actual
preserved build and executable hashes. A newly assembled VPK has not itself
been installed through VitaShell in this session.

## Performance and next hardware test

The previous representative measurement is still **11.00 FPS at 640×360**.
No new physical Vita performance gain is claimed. The user reports much better
rendering and that 360p resolution has the clearest effect among the settings.

Keep the currently installed build for the next controlled test: use
**L + R + Square** in a stationary gameplay view, then play with turns, firing,
driving and death/respawn. Reconnect USB for log/crash collection afterward.
Compare total frame duration and visibility waits before selecting the next
candidate. The separate opt-in CPU experiments remain locally tested and await
hardware comparison. Stable representative 20 FPS remains the target.
