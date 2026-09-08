# Unused render-target bindings — September 6, 2026

The ordered render-target scheduler rejected a draw whenever any recorded
texture binding pointed to its destination. Halo leaves textures bound across
shader changes, so an unused stage could reject a valid draw. Clear commands
also retained unrelated bindings despite not sampling textures.

Feedback detection now happens in the draw path after selecting the linked
fragment program. Only active sampler stages are considered, after resolving
previous-frame replacement and cube/2D fallbacks. Actual sampling of the active
offscreen color target remains blocked. An unavailable fallback also skips the
draw instead of leaving an old sampler binding active.

The render-target lifecycle regression includes a producer draw with its own
destination bound only at unused stage 3. It fails against the old scheduler
and passes with the correction. A separate test executes the production texture
binding helper under ASan/UBSan and covers active feedback, sparse/remapped
samplers, previous-frame replacement, cube-face aliasing, kind mismatches,
unset textures and fallback allocation failure. The native Vita build passes.

The main menu, Blood Gulch charged plasma shot, and campaign cryo-room
resume render with the correction in Vita3K. Hardware validation continues. This fixes a draw-rejection condition; it does
not by itself establish the cause of every reported bloom or transparency bug.
