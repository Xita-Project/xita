# Opaque material hardware comparison — September 7

The new optimization shows a **small positive result in this run**: 7.079 FPS
on versus pooled off throughput of 6.803 FPS, a **4.06% increase** or **5.73 ms
less per frame**. It beats both off phases. Matching shader counters and lower
graphics-completion waiting support the result. This is one short comparison,
not proof of repeatability across views or progress to sustained 20 FPS.

## Verified collection

Read-only USB archive:
`/home/birchwoodgod/xita-backups/2026-09-07-142628-opaque-material-hardware`.
The 14:27 CDT collection copied and hash-verified 63 files, 487,020,592 bytes.
The executable matches the [validated material build](opaque-material-candidate-20260907.md):
32,918,474 bytes, SHA-256
`e2f70270c1bf472149dda554f91b33963737d59bca392cbc2bf22d0ecc398258`.

Saved settings remain byte-identical to installation: 544p, textures 256,
automatic filtering, mip smoothing On, High effects, CPU 444 MHz, 20 FPS cap,
rear touch disabled. The test logs `[material-test]`, confirming the new
L+R+Square / GPU comparison ran. No device files were written. USB is safely
unmounted. No new screenshots or geometry diagnostic samples were available.

## Exact off/on/off result

Each phase settles for 60 frames and measures 120 frames at fixed 544p.

| Phase | FPS | Mean frame time |
| --- | ---: | ---: |
| Off before | 6.730 | 148.582 ms |
| On | 7.079 | 141.265 ms |
| Off after | 6.877 | 145.413 ms |

Pooled off throughput is 6.803 FPS / 146.998 ms. On improves by 0.276 FPS /
4.06%. The two off phases differ by 2.18% / 3.169 ms, so the comparison contains
drift as well as the optimization's signal. Do not describe 4.06% as a universal
or statistically established speedup. It is also not valid to compare these
absolute numbers with earlier builds' benchmarks from different camera views.

Position `105.0316 -156.4815 0.8184` and forward
`-0.72774 0.68589 0.00000` match in every phase, with `view-ok 1` throughout.
Supporting reports show 226 draws/frame in all phases. Simulation continues;
equal camera/draw totals do not freeze every workload detail.

## Work and timing evidence

Each measured phase contains two complete 60-frame reports. Their boundaries
differ from the exact FPS interval; settling keeps their counters within the
selected phase's path.

| Work per 60-frame report | Off before | On | Off after |
| --- | ---: | ---: | ---: |
| Eligible material draws | 3,480 | 3,480 | 3,480 |
| Captured opaque proofs | 0 | 1,920 | 0 |
| `154066FD` draws / indices | 3,540 / 830,820 | 3,540 / 830,820 | 3,540 / 830,820 |
| Material draws using alpha-disabled program | 60 | 1,980 | 60 |
| Integer-local clipping calls | 39,300 | 39,300 | 39,300 |
| Palette hashes reused / computed | 1,200 / 0 | 1,200 / 0 | 1,200 / 0 |
| Texture stages prepared / unused skipped | 38,820 / 14,760 | 38,820 / 14,760 | 38,820 / 14,760 |

The optimization removes alpha testing from **32 additional draws/frame** in
this hardware view; 26 draws/frame in that family still require the original
path. The earlier emulator view's 54-draw reduction was view-specific.
Earlier CPU optimizations stay enabled throughout, and completion restores the
material optimization's configured default. Later reports verify it stays on.

| Supporting elapsed metric | Off before | On | Off after |
| --- | ---: | ---: | ---: |
| Draw preparation | 12.12 ms | 11.67 ms | 9.91 ms |
| Render submission | 6.16 ms | 6.18 ms | 6.11 ms |
| Final graphics-completion wait | 73.65 ms | 67.94 ms | 73.65 ms |
| Query waiting per guest frame | 35.38 ms | 32.08 ms | 36.57 ms |
| Nearby median C0 / C1 / C2 busy | 4 / 9 / 70% | 4 / 11 / 70.5% | 3 / 7 / 71% |

The final graphics wait drops by 5.71 ms / 7.75% against the off mean, then
returns when the optimization turns off. Submission stays near 6.2 ms.
Draw preparation decreases across the run, including the final off phase;
do not attribute that entire decline to this shader selection change.
Graphics/query waits overlap CPU work and one another. They are elapsed waits,
not exclusive GPU timing, and must not be added to reconstruct frame time.

The measured texture pool stays at 132 textures / 8,489 KB, with zero decodes,
refreshes or purges. No pool churn from pinned uploads appears in this short
view; dynamic scenes still need monitoring. All 39 complete render reports have
consistent stage sums, 60 final completion/queue calls and zero intermediate
target Finish calls. No automatic draw-shortage, current shader/target failure
or work-table overflow is logged. Historical `xboxvita*` error logs remain
byte-identical to their predeployment backups. This log check is not a visual
correctness certification.

## Next development step

Retain this validated optimization and keep standard settings fixed. **No
additional identical benchmark is needed now.** The next source/prototype task
is to inspect the 26 remaining alpha-tested draws in the same material family:
determine whether their uploaded alpha range lies wholly on the passing side
of the captured cutoff. Fractional alpha alone need not make a test necessary.
An interval that crosses the cutoff must keep the original shader. Capture
eligibility counts before assuming the broader proof will help, preserve upload
lifetime and mip/sampler guarantees, then validate any extension locally.

Do not blindly apply the tex0 opacity rule to the next common pass. Source
inspection of `8ED40330` / `ps_D7B21DB5_0C` shows output alpha depends on two
texture samples and captured constants; tex0 opacity alone is insufficient.
That family retains alpha testing on 18 draws/frame / 11,391 indices/frame in
this view. Its equal work totals with `A01D09CF` do not prove either pass is
redundant. Shader/pass work counts are investigation targets, not GPU cost
rankings or permission to remove lighting.

The archive includes raw files, hashes, exact phase analysis, switch/texture
verification, log inventory and reproducible scripts. Game code and the installed
executable were unchanged during this collection. The next candidate should
compare only its additional change with the current optimization still enabled.

Follow-up: the [remaining-alpha upload audit](alpha-range-audit-20260907.md) found
no additional whole-texture interval candidates in its three Blood Gulch and one
cryo capture. Next validate the existing dedicated GREATER shader separately.
