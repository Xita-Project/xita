# Query-only vertex-distance reconstruction

`XV_QUERY_SEMANTIC_LEAF=1` selects one qualified arithmetic interval inside the
separate collision-query unit. The repository default is **0**. It requires
`XV_QUERY_F32_INLINE=1`, which already requires the guarded Halo CE query fusion
and its native traversal prerequisites. All twelve existing cumulative settings
remain independently selected; this option does not replace any of them.

The change uses scalar VFP locals for the four SSE squared differences, then
reconstructs every affected XMM lane. It retains all ten floating-point
operations, including the padding lane, and pins the actual retained ARM
addition operand order for NaN payloads. The original guest loads and ordered
distance store remain in place. It changes no x87 precision, flags, stale
context slots, roots, callbacks, generic fallback, caller, solver, shared header,
worker transaction or lock ownership. It does not discard guest scratch writes.

The qualified private prototype reduced modeled whole-query ARM instructions
by about 2.0–2.5% on busy depth-16 cases relative to the already-inlined query.
This is a small cumulative candidate, not evidence of an FPS gain. The query
text grows from 33,624 to 34,140 bytes; its native frame grows from 2,136 to
2,176 bytes. The adapter remains 24 bytes. These are the compiler's static local
frames, not a whole native call-chain or thread high-water measurement.

## Generation and builds

Use the existing twelve-path build command with `XV_QUERY_SEMANTIC_LEAF=1`.
`tools/gen_native_query_fusion.py` first performs the unchanged owned query,
caller, optional solver and query-f32 generation. The small
`tools/query_semantic_leaf.py` transform then verifies and replaces exactly one
interval at `NQ_CV_vertex`. It proves its source edit reversible and emits
`recomp/query_semantic_leaf.h` from the authored `tools/query_semantic_leaf.h`.
Only the query unit includes that generated header.

Make tracks the authored module/header, generated header and a content-stable
`query-semantic.config` selector stamp. Missing generated output forces
regeneration. Repeated unchanged builds do not compile. All input and transform
validation completes before any generated output is published; the receipt is
written last. Invalid boolean values and unmet prerequisites fail explicitly.
OFF regenerates the exact current twelve-path query source and full object.
An unused ignored generated header may remain on disk after OFF.

When refreshing a retained stage, copy the Makefile, generator, semantic module
and authored header together. The semantic module is imported even while OFF;
the authored header is consumed only while ON. No caller, solver or shared
runtime source change is required. The existing shared generation stamp can
recompile the unchanged caller and enabled solver on a full application build;
their source bytes remain identical. Graph fixtures retain that behavior.

## Qualification

The original private prototype is commit `454ebe2b52c991841e7296959c302521147d339c`.
It compared full context, guest arena, page tables/roots, native FPSCR, original
pointer observations and ordered guest writes. Its 33 complete-query cases
include 316 observer snapshots per lane. Thirty additional exceptional-input
prefixes execute the actual retained query objects up to the matched first
existing preempt boundary after the distance store. These stop before invalid
geometry can fault later in the generic query. They are prefix qualification,
not successful complete exceptional-geometry traversals. The 210 supplemental
arithmetic checks are not the sole authority for NaN payload ordering.

Production integration reuses this oracle only after comparing every allocated
object section, normalized relocation, import and stack-frame report against
the qualified semantic object. It separately checks exact OFF source/full-object
restoration, the 71 other retained recompiled object identities, missing-output recovery,
authored-input dependencies, no-op builds and rejection before publication.
There is no new semantic-suite, device, Vita3K, benchmark or performance claim.
The original query/solver Make graph tests remain separate from actual ARM
compilation and do not serve as semantic oracles.

```sh
python tools/test_query_semantic_integration.py \
  --retained-build "$TWELVE_PATH_BUILD" \
  --build-command "$TWELVE_PATH_COMMAND_JSON" \
  --qualified-dir "$SEMANTIC_PROTOTYPE_COST" \
  --out "$PRIVATE_OUTPUT/production"
```

This needs owned inputs, the Vita toolchain and Python ELF/recompiler packages.
Outputs remain in a new private directory. No owned code or binary is committed.

## Native floating-point trap limitation

The fast interval requires `(FPSCR & 0x00009f00) == 0`. If native exception traps
are enabled, the helper retains the original C arithmetic/store sequence. That
branch has only been statically reviewed: the instruction runner used for the
prototype clears the trap-enable bits on readback. Its seven attempted native
trap probes were recorded as unsupported, not passes. No native exception
handler or interrupted-instruction context equivalence is claimed.

The production-source audit found no game-controlled native trap setter:

* `recompiler/xita_recomp.py` emits guest `FLDCW` as `c->fcw = X_M16(...)`.
  The retained owned units contain 27 such assignments, including duplicated
  translated entry regions. They do not write ARM FPSCR. Guest x87 control and
  ARM trap control are distinct; the existing x87 model is unchanged.
* `xk_hierarchy.c` reads native FPSCR, rejects enabled traps and restores that
  captured value only on numeric decline.
* The optional quaternion cache preserves control bits and changes only sticky
  or condition bits. Optional query/cluster replay code restores environments
  obtained with `fegetenv`; it does not synthesize exception-enable bits.
* The final twelve-path `xita.elf` disassembly contains one direct
  `vmsr fpscr`, inside the hierarchy helper's saved-state restoration. No guest
  generated unit contains a native FPSCR setter or `fesetenv` call.

This establishes that the reviewed game/runtime does not itself turn masked
native traps on. It does not prove operating-system, SDK, plugin or debugger
initial state and cannot qualify exception handlers. The runtime trap check
therefore remains required. The earlier strict full-state observation and
alias contract is retained without weakening it for this integration.

## Private production result

The isolated integration is based on `a60a53f`. The actual retained twelve-path
Make command keeps every feature selection and adds only the new selector.
The focused matrix passed 14 build/no-op rows and 12 rejection checks. The
original query and solver graph fixtures also passed. The initial matrix
unnecessarily rebuilt the unchanged large caller on every probe and was stopped;
its passing default compile and partial artifacts are preserved separately.
The final matrix compiles only the changed query and compares all 71 other
retained recompiled objects and caller/solver sources byte-for-byte.

The resulting query object SHA-256 is
`13684fe281c36823c29b3ff6eda44eaef8d9ca10067321f668545a5dddeb8150`.
Its 34,140-byte `.text` SHA-256 is
`959993a9f34f47cf311e7424e5eff112874d4eacb4e4fe743d14dff8ab899596`.
All allocated sections, normalized relocations, undefined imports and every
reported local stack frame match the qualified prototype object. The authored
and generated helper are byte-identical to the qualified prototype header,
SHA-256 `6137c6e195ec68c644383f1fd88e2660390564a9268df67d4cb5aa9ce4efdb7c`.
OFF restores full object SHA-256
`c1717b6fdd4e5953cc6e1823caf8a9407e645ee06334e566f95e38f95e93cd2d`.

Evidence lives under the private `query-semantic-production-integration/`
directory: `production-focused/result.json`, `commands.json`, ON object/source/
header/stack files, the final generation receipt, graph-fixture results and
`fpscr-census.json`. No production source or device was changed by this work.
