# Captured effects follow-up — September 6, 2026

A new surface-description candidate run, including Snipers, campaign resume,
and plasma shots in Blood Gulch, recorded six vertex/combiner combinations
without a canonical runtime entry. The ordinary texture fallback cannot
reproduce their alpha-only outputs, constant colors, multi-input blur, or
reflected model lighting.

| Vertex program | Raw combiner | Canonical key | Behavior |
| --- | --- | --- | --- |
| VS56 | 9B13813A | 19AB9BBB | Zero RGB, constant alpha |
| VS56 | D26C975A | 037C4AAB | Zero RGB, four times texture alpha |
| VS56 | 1E711638 | F648D0A1 | Texture-driven two-color flare, vertex alpha |
| VS38 | 1E073CA3 | 51622122 | Texture-0 alpha in RGB, scaled texture-1 alpha |
| VS38 | 74D0C65E | 63C01584 | Four-input filtered composite |
| VS09 | E519BFF9 | E519BFF9 | Model texture, lighting and reflection combiner |

Seven new fragment programs cover these pairs, including the final program's
cube-on-2D variant. Sources come from the existing generator and captured
definitions. Every previously present shader source remains byte-identical.
The native compiler reports seven successes and zero failures. The old table
fails the added real-lookup regression; the new table passes it, shader package
checks, 4,420 C/Python identity checks, 2,029 source-equivalence cases, and
ASan/UBSan link/retention tests. The native Vita build passes.

Separately, a 60 Hz window recording of the preceding candidate now confirms
normal plasma bolts moving away from the muzzle, their wall impacts, and the
released charged orb followed by its impact. These effects were already
visible before this six-pair addition; this evidence extends the earlier
charge-glow observation without attributing the projectile recovery to the
new assets. The recording also shows translucent green discharge effects.

The candidate retains campaign glass and characters, normal/charged plasma
shots, and unzoom/2x/10x scope transitions in Vita3K. A campaign trace checked
606 draws with no data mutations and no frame-storage shortages. The zoom
trace also revealed a remaining single-texture UI fallback; the separate
[screen-composite routing correction](screen-composites-20260906.md) addresses
that path. Hardware effects, sustained performance, and broad smoke/bloom
coverage remain pending.
