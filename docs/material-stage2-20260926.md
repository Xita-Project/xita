# Outdoor material stage-2 candidate

Status: optional runtime selection implemented; host and Pi checks pass. GXP
compiled privately. No Vita application build/deployment or FPS claim yet.

The complete perf263 outdoor traces contain 149 material `154066FD` draws at
frame 11098 and 169 during firing at frame 11460. Of these, 81 and 96 respectively
have exact captured `psc[1].rgb == 0` and `psc[2].rgb == 1`. These constants
potentially make stage-2 texture color irrelevant in the already qualified
axis/black shader. This is a better admission condition than texture dimensions.

The repeated 4x4 stage-2 descriptor at data `62B68940` is associated with the
pine trunk resource `803E84C8`. The owned map's pine trunk shader references a
256x256 base and 256x256 detail bitmap; it does not establish the content of the
runtime 4x4 descriptor. Do not hardcode its address or assume a neutral texel.

## Algebra and limits

With the existing axis/black conditions, stage 1 becomes saturated vertex RGB
plus zero, and stage 2 becomes `(1 - saturate(t2.b)) + saturate(t2.b)` in each
RGB channel. The remaining stage-2-dependent alpha feeds a term multiplied by
proven black stage 3; output alpha remains tex0 alpha. The candidate substitutes
only the two constants and leaves the original arithmetic and alpha discard
intact for the compiler to simplify.

A supporting CPU float32 check covered 256 normalized byte values and 1,000,000
seeded nonnegative finite float32 values through 1.0: zero cancellation
mismatches. This is not proof of GXM precision, exceptional varying behavior,
or whole-shader equivalence.

Private evidence under `../palette-lru-candidate/`:

- `stage2-map-evidence.json`: owned map resource and bitmap mapping.
- `stage2-constant-analysis.json`: accepted trace command IDs and arithmetic check.
- `ps_154066FD_7F_t8_axisblack_gt_nocolor.frag.cg`: compiled prototype.

## Compiled result and integration

Isolated XVSC00001 compiler run completed 18 shaders with zero compiler failures.
Its emulator process exited 133 afterward with the previously observed shutdown
fault. The candidate GXP was produced successfully (1116 bytes, SHA256
`98acf42bafcd6ce8572e4deff73f1a3c660b69dc25bcbba2c7af3cc08a4a9992`).
No emulator gameplay/performance test was performed.

Compared with the existing 1228-byte program:

| Field | Existing | Candidate |
| --- | ---: | ---: |
| Primary instructions | 78 | 70 |
| Secondary instructions | 21 | 14 |
| Temporary registers | 4 | 0 |
| Primary registers | 16 | 16 |
| Secondary registers | 82 | 83 |
| Phases | 2 | 2 |
| Texture flags | 0x111 | 0x111 |

The compiled program still declares all three nondependent texture reads. Do
not claim texture elimination: the observed improvement is arithmetic/register
reduction, with one additional secondary register. Header interpretation follows
[Vita3K's GXM program definition](https://github.com/Vita3K/Vita3K/blob/master/vita3k/gxm/include/gxm/types.h).

`XV_MATERIAL_NOCOLOR=1` opts into mode 8, exclusively from already admitted mode
6 and bit-exact positive-zero PSC1 RGB / one PSC2 RGB. Signed zero, adjacent
values, NaNs and infinities fail admission. Diagnostic mode 7 takes precedence.
The linker cache distinguishes mode 8, retains GREATER alpha policy, and falls
back 8 -> 6 -> 5 -> ordinary shader if optional programs fail. No texture-mask
reduction is made. The embed tool includes the optional program when present.

`tools/specialize_ps_nocolor.py` accepts only the audited input SHA and reproduces
the compiled candidate exactly. Modified source rejection was checked. Host
ASan/UBSan tests cover actual production linker caching/fallback and constant
admission. Both fixtures also passed as static ARM executables on Pi core 0
(`stage2-pi.log`); these are correctness checks, not Vita performance predictions.

Next: package a private Vita candidate retaining the qualified stack, verify
actual selection and tree/alpha rendering, then collect ordinary gameplay frame
times. This has not yet changed the installed perf263 build.
