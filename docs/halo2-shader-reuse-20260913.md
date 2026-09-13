# Halo 2 shader compiler reuse audit

The existing Xbox vertex-program decoder and Cg generator can process the two
Halo 2 programs inspected so far. Both generated sources compile to Vita GXP
with `libSceShaccCg`, with no compilation failures. This validates a reusable
compiler path; it does not establish correct drawing, complete shader coverage,
or a working main menu.

The inputs came from the locally owned Halo 2 executable and a private native
startup command capture. Neither the shader code nor the diagnostic packages
are included in the repository.

| Sample | Evidence | Compilation result |
| --- | --- | --- |
| Constructor program | Twelve complete instruction slots uploaded during native attempt 28; all four words present in each slot | 660-byte GXP; one scalar-varying allocation warning |
| Executable candidate | One 21-instruction XVS candidate found by the existing heuristic executable scanner at `0x0043F2B0` | 912-byte GXP; no compiler warning |

The uploaded program differs from the executable candidate. The executable scan
found no declaration candidates and is not an inventory of programs loaded from
maps or constructed at runtime. Both decodes produced no decoder warnings. The
generator inferred FLOAT4 attributes in the absence of declarations: ten inputs
for the constructor and three for the executable candidate. Those guesses are
not validated vertex bindings.

## Position conversion needs draw-state evidence

The shared generator recognizes Halo CE's particular viewport sequence using
hardware constants 58/59 and a reciprocal in `r1.x`. The Halo 2 executable sample
uses constants 10/11 and `r1.w`, with the reciprocal in a separate earlier
instruction slot. The current detector does not remove that sequence.

That is a possible duplicate viewport/perspective conversion when feeding GXM,
but the actual constants and viewport at a draw are needed to prove the intended
conversion. Removing the whole reciprocal slot would also remove an unrelated
multiply used for texture coordinates. Any future rewrite must preserve the
other operation in a dual-issued instruction. The constructor's separate
position path likewise needs validation against its actual pipeline state.

Before implementing a rendering binding, capture the active program, complete
constant bank, execution mode, vertex-array formats/strides/offsets and current
attributes, textures, combiner state and viewport at an actual draw. Derive
bindings from that state rather than assuming Halo CE's shader catalogue or
register conventions apply to Halo 2. Compare the resulting positions and UVs
before enabling a generalized viewport rewrite.

## Validation record

The independent compiler application ran in a separate Vita3K data directory.
Its log reports two successful compilations and zero failures. After the utility
closed, Vita3K faulted during relaunch/idle; this is recorded separately from the
successful compiler results and is not a Halo 2 gameplay crash.

| Private output | SHA-256 |
| --- | --- |
| Constructor GXP | `f9a74c7a8c7dea7fe9b24f284f3a802197f78fc6fd0669db681235821dacaf14` |
| Executable-candidate GXP | `59fb4f7977bf842f3f932753016e9473026832be57681045278ae459995d4444` |
| Compiler log | `62c16da3d2d943b9ffdd5f2c2f221744516108bc1eb8b2580089e969ff64b516` |

No shared shader translation behavior changed in this audit. There is no
hardware performance result from these compilations.
