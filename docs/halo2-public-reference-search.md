# Halo 2 public reference search

2026-09-26: delegated read-only GitHub/web search found no verified standalone
Xbox Halo 2 executable decompilation or static recompilation project. This is
a search result, not proof that none exists. No Halo 2 runtime changes made.

Priority references:

1. [Halo-2-HD](https://github.com/grimdoomer/Halo-2-HD): original Xbox x86
   patches, MIT; releases distinguish versions 1.0 and 1.5. Its
   `src/halo2_hd_patches.asm` provides rasterizer/render-target and texture/
   geometry-cache anchors and register calling conventions. Match the local
   XBE version before using any address. It is not a portable engine.
2. [Cartographer](https://github.com/pnill/cartographer): GPL-3.0 Halo 2 Vista
   mod DLL with reconstructed engine code and calls back into the original
   executable. Inspect `xlive/Blam/Engine/main/main_render.cpp` and
   `xlive/Blam/Engine/cache/cache_files.cpp` on `development-new` for semantics.
   PC addresses, layouts and D3D9 behavior are not Xbox guarantees.
3. [Assembly](https://github.com/XboxChaos/Assembly): GPL-3.0 cache editor.
   `src/Assembly/Plugins/Halo2Xbox` and `src/Blamite/Formats/Halo2Xbox` are
   Xbox-specific asset/layout references, not executable reconstruction.
4. [OpenH2](https://github.com/ronbrogan/OpenH2): MIT C# engine recreation,
   with dependency notices. Vista/MCC map assumptions require checking.
   Its README explicitly distinguishes it from game-binary reverse engineering.

Suggested startup investigation: use Xbox patch symbols to identify real
render-target/cache initialization boundaries, compare their behavior with
the local startup trace, and consult Cartographer only after matching semantic
roles. Validate Xbox cache structures independently with Assembly. Do not
replace the existing Halo 2 worktree or run competing emulator/Pi jobs.

Excluded false positives: zcash/halo2 is cryptography; H2Codez modifies the PC
editing kit; Halo CE reconstruction projects do not establish Halo 2 support.
