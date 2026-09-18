# Geometry and visibility optimization goal

Agreed September 17, 2026. Targeted reverse engineering is the next focus toward
stable 20 FPS on physical Vita hardware, retaining standard graphics and the
compatible cumulative optimizations already installed. The user reports that
crashes are resolved; preserve that stability.

## Work to complete

1. Trace Halo CE's actual vertex/index producers, consumers, aliases and resource
   lifetimes. Identify every relevant writer and invalidation boundary before
   removing repeated validation or retaining guest geometry across frames.
2. Map world visibility inputs, outputs and dependencies. Identify repeated work
   that can be removed and larger independent operations that can run on worker
   cores without changing game state or render ordering.
3. Implement candidates supported by those findings, keeping compatible changes
   together. Check memory ownership and behavior with focused local validation.
4. Verify the installed executable and restart the process before ordinary
   hardware gameplay. Compare representative Blood Gulch valley, driving,
   effects and campaign scenes at native resolution and standard settings.

The performance target is sustained gameplay around a 50 ms frame budget,
including demanding scenes. Occasional 20 FPS, menu/sky views, increased core
utilization and unmeasured optimizations do not establish success. Record frame
time variation and rendering/gameplay regressions alongside averages; do not
infer gains from different camera positions or add unrelated FPS results.

## Starting evidence and next investigation

The current retired vertex snapshot optimization avoids copying unchanged data
but still validates referenced bytes. A captured heavy valley report contained
14.316 ms in stream preparation within 30.9 ms of draw HLE elapsed time. These
are nested elapsed measurements, not additive CPU self times, and do not prove
that all those streams are static world geometry.

Begin by mapping game callers of SetStreamSource (`0x183AD0`), resource Register
(`0x184AB0`), CreateVertexBuffer (`0x185870`) and vertex Lock (`0x1858D0`). Connect
their resource ranges to owned map data and actual writers. Halo's visible-index
builder at `0x53FA0` rebuilds selected triangle indices, so static terrain alone
does not justify treating its submission buffers as immutable. Lock history is
also insufficient: guest code can retain writable pointers.

## Working constraints

- Preserve the installed cumulative build until a replacement is qualified.
- Use ordinary fresh-launch hardware gameplay; no per-change built-in benchmarks
  and no Halo CE Vita3K validation.
- Keep work local and focused to limit usage; avoid unnecessary agent runs.
- Keep source, findings and build receipts backed up privately. Do not publish
  owned game data or change repository visibility.

The automated goal tracker now has this objective active. The initial replacement
attempt was refused while the earlier goal remained unfinished; the user then
established the new goal. See the [first ownership findings](geometry-ownership-20260917.md).
