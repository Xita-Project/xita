# Ancestor scalar query production wiring

`XV_QUERY_ANCESTOR_SCALAR=1` selects the reviewed ancestor scan only inside
`query_fusion.o`. The repository default is `0`. It requires
`XV_QUERY_MEMBERSHIP_SCALAR=1`, retaining the existing semantic-leaf, F32-inline,
and native-query prerequisite chain. All eighteen current cumulative paths are
preserved; this selector is the only added setting.

The dependency-free `tools/query_ancestor_scalar.py` uses the unchanged authored
replacement from prototype `dd7cd86c6a2c36d618976ad42fcb0395c320cb54`.
It pins the original interval hash, rejects external interior entries and
previous transformations, and verifies the qualified composition markers.
The shared generator applies it last and publishes only after all contracts
pass. No shared header, caller, solver, or generic fallback is changed.

A content-aware `query-ancestor.config` stamp participates in generation for
both ON and OFF transitions. The authored Python module is a generation input;
a missing query source uses the existing regeneration path. The C define is
local to the query object's compiler recipe. OFF restores the exact current
source and full object.

## Bounded build qualification

`tools/test_query_ancestor_integration.py` copies the actual eighteen-path
`render-preparation-startup/build` and preserves its exact feature command,
including retained texture state and depth preparation. It compiles the query
only. Fourteen build/no-op rows cover default, enable/disable, missing generated
source, authored transform change/restore, and final enable. Fifteen negative
checks cover invalid selectors, every prerequisite in the chain, exact interval
drift, invalid composition, an external interior entry, a missing authored
module, and optimized Python. Failed generation leaves outputs unpublished.

| Query object | SHA-256 | Text bytes |
| --- | --- | ---: |
| OFF, exact retained eighteen-path object | `600ff70a5511148d7628f76768954b7ba3aec791924a19a5dedb4a4c6af50e4e` | 33988 |
| Production ON | `b866e64bee97b74ccec9298e2f019515d1897ae0e177066c1f82bd246ba58290` | 34060 |

ON's allocated sections, normalized relocations and imports exactly match the
qualified prototype. Its text SHA-256 is
`c4d5945a7715d545cef45ad9e0408bc00492a6a30b0055a895a3dfd24a003724`.
The query frame is 2168 bytes and adapter 24 bytes. Six retained
caller/solver/generic source files and 71 other compiled objects are verified
unchanged throughout; they are preserved, **not recompiled by this test**.
The root's complete cumulative package build is a separate check.

The existing query and solver Make graph tests also pass using real host
compiler/archive/dependency recipes and synthetic owned inputs. Earlier
integration fixtures receive only the newly required module in their copied
input inventory. No semantic fixture suite is rerun: exact object equivalence
reuses the prototype's 60 full-query cases, including its real callback and
continuation-overflow coverage.

## Scope and expected benefit

See [the prototype report](QUERY_ANCESTOR_SCALAR_PROTOTYPE.md) for the complete
state recipe and adverse costs. Modeled whole-query instruction changes are
−2.15% in the ordinary depth-16 case, −2.83% outside one surface, and −3.06% in
the tested depth-40 fallback case. The short four-surface case regresses 0.73%;
the combined case changes by +0.015%. These are not cycle or hardware FPS
measurements and do not establish a universal speedup.

The integration does not broaden ownership, discard context, reduce FP
precision, add future guest reads, or move callbacks. It performs no device
access or gameplay and leaves production enable to the root's reviewed build.
