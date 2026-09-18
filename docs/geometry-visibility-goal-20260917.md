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

The [captured vertex-preparation trial](captured-vertex-preparation-20260917.md)
now overlaps preparation across draws on hardware while retaining private input
copies and GPU retirement. Its initial gameplay smoke passed; an FPS gain has
not been established. Next investigate which loaded resources can safely avoid
those copies, and validate representative gameplay before claiming the target.

The [visibility dispatch audit](visibility-dispatch-20260917.md) now separates
the two original world-culling paths across all 82 owned BSPs. Blood Gulch uses
subcluster bounds; the qualified triangle-outcode prototype remains uninstalled
because it does not target that path. Continue with portal clipping/subcluster
work and the remaining retained-pointer lifetime questions.

The next [clipping input-span candidate](clip-distance-spans-20260917.md)
reduces repeated guest address translation within the active portal clipper.
It preserves the original math and fallback, and keeps earlier optimizations.

The [typed portal-polygon experiment](typed-portal-polygon-20260918.md) now
replaces the internal emulated scratch/register machinery with a native data
interface in local tests. Geometry and whole-traversal output comparisons pass,
with a substantial modeled instruction reduction. Production admission,
worker/scheduler qualification and hardware timing remain before deployment.
