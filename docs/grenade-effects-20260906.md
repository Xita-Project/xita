# Grenade smoke and model lighting — September 6, 2026

A fourteen-second Blood Gulch Snipers recording of the visibility-lookup
candidate captures two fragmentation grenades. Both produce bright flame,
dark smoke and brown dust that expand and fade. The base remains visible through
the fading effect, and the recording shows debris. This confirms local smoke
transparency for this effect; it does not establish all smoke or bloom fidelity.

That same run logs 108 distinct vertex/combiner pairs. One lacks a runtime entry:
VS10 (`393556F8`) with combiner `E9632F91`. The trace records the renderer using
its one-texture fallback for this eight-stage model-lighting material during
the grenade flash. The captured definition already exists for other vertex
programs, but VS10 needs the variant matching its actual outputs.

The pair is now included in the accumulated shader inputs. The existing generator
produces two fragment variants for cube and 2D reflection textures; all previously
present shader sources remain byte-identical. Native compilation reports two
successes and zero failures. The actual runtime-lookup regression fails with the
old table and passes with the new table. Shader-package, HUD routing, link/cache
and frame-retention checks, ASan/UBSan, 4,420 identity checks and 2,031 source
equivalence cases pass, as does the native Vita build. In the native candidate,
the new fragment program links against VS10 and the grenade flash retains the
weapon, scenery, flames, smoke and fading dust. All 64 observed pairs in this
fresh run have canonical table coverage. Two diagnostic frames check 331 draws
with zero data mutations, and the run logs no draw-storage drops. Hardware
appearance and performance remain pending.

The smoke was already visible before adding this material pairing. Its purpose
is to preserve the complete lighting combiner during the grenade flash.
