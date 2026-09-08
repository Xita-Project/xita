# Sniper scope mask — September 6, 2026

The saved September 5 sniper trace uses combiner `FFBC5A4D` for the two vertical
scope markings. A fresh Snipers match reproduces the unzoomed markings with
`40BF35E1`, whose active combiner is identical; only unused texture modes differ.
The first-person arms are visible in this fresh baseline with the earlier fixes.

The three-stage program multiplies the texture-0 HUD mask and its constant by
the texture-1 marking image and its constant. The one-texture UI fallback chose
texture 1 alone, discarding the mask and dynamic opacity. The HUD recognizer now
routes this exact active program through the mesh bridge with both textures,
original vertex transforms, constants, alpha testing and blending. It requires
PROJECT2D for both sampled stages and accepts harmless unused stage changes.
Only the captured VS03 pair is enabled, preserving the menu glyph path.

One captured fragment program is newly embedded. Native SceShaccCg compilation
succeeded with zero failures. The old recognizer fails the captured regression;
the revised recognizer passes, including active-stage mutations, constants,
unused texture modes and the current capture's `0x21` mode word. Shader/cache,
4,404 C/Python identity checks and 2,022 source-equivalence checks pass. Existing
fragment sources are unchanged. The Vita executable builds successfully.

Vita3K confirms the vertical markings are absent while unzoomed, present at
2x and 10x, then absent again on returning to first person. The rifle, arms,
ammo HUD and world remain visible. Hardware validation is pending.
