# Ordered query membership integration

`XV_QUERY_MEMBERSHIP_SCALAR=1` enables the qualified scalar reconstruction only
inside the separate native query unit. It defaults to `0` and requires
`XV_QUERY_SEMANTIC_LEAF=1`, which retains the existing F32-inline and native-query
prerequisites. All fifteen current cumulative paths remain enabled in the
qualified build command; this selector is the only addition.

The dependency-free `tools/query_membership_scalar.py` contains the unchanged
replacement qualified in `fab841c8c56d9ae10a3da5faa80765d1297013a3`. It pins the
original generated interval by SHA-256 and rejects unexpected interior entries,
prior transformations, or a missing semantic/F32 composition. The shared query
generator validates everything before publishing its output. No shared runtime
header, caller, solver, generic fallback, FP operation, guest write, lock, or
callback is changed.

The Makefile applies the new C define only to `query_fusion.o`. A content-aware
`query-membership.config` stamp causes regeneration on both ON and OFF
transitions; the authored transform is a generation input. Existing missing
output detection regenerates a deleted query source. OFF restores the exact
previous query source and complete object.

## Private validation, September 17

`tools/test_query_membership_integration.py` uses a separate copy of the retained
fifteen-path production build and the actual ARM compiler. Fourteen build/no-op
rows cover default, ON/OFF, missing query source, authored input change/restore,
and a final ON transition. Fourteen negative checks reject invalid selectors,
missing prerequisites, source interval drift, interior entries, missing semantic
composition, a missing authored transform, and optimized Python.

| Object | Full object SHA-256 | Text bytes |
| --- | --- | ---: |
| OFF, exact current fifteen-path object | `13684fe281c36823c29b3ff6eda44eaef8d9ca10067321f668545a5dddeb8150` | 34140 |
| Production ON | `600ff70a5511148d7628f76768954b7ba3aec791924a19a5dedb4a4c6af50e4e` | 33988 |

ON has exactly the qualified prototype's allocated sections, normalized
relocations, and imports. Its text SHA-256 is
`06b9b249c7a014175c27d624ec682c014d9f17cd2fc609f5efac05f52336d0fa`.
The query frame remains 2176 bytes and adapter frame 24 bytes. The private test
compiles the query only: the six caller/solver/generic source files and all 71
other retained objects are verified unchanged, **not recompiled**. The root's
complete cumulative build is a separate validation.

The actual Makefile's query and solver graph tests also pass, including real
host compilation, archives, no-op, missing-output and profile transitions with
synthetic owned inputs. Their fixture now includes the existing packed-vertex
header dependency. The first graph attempts stopped on that missing fixture
placeholder before the correction; they did not expose a production defect.

The prior 59 exact-state ARM cases are reused by object equivalence; they were
not rerun. This does not widen their callback, FPSCR, or ownership contract.
The four-surface whole-query instruction reduction remains 4.63%, with smaller
wins and small regressions in other cases, as documented in the prototype.
These are modeled instructions, not measured hardware frame-time gains.
This integration performs no device access, gameplay, or production enable.
