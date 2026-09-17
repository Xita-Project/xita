# Collision solver callback qualification

The private solver prototype from `1cd9b9a` now passes a targeted qualification
covering all **14 expired-budget yield callbacks**. No callbacks remain
unobserved in this matrix. This follow-up changes the fixture and validation
tools, not the fused algorithm, production configuration, or device build.

The candidate's 73,532-byte compiler text section, including padding, is
byte-identical to the original prototype's retained-object candidate. Its
generator function is unchanged. The earlier code-size, stack, copy-traffic,
and stationary-path costs therefore remain relevant; this is correctness
evidence, not another performance or hardware FPS result.

## Complete-call comparison

Each case executes the real `172CB8` caller suffix, the whole solver, its
visitors, result stores, and the caller continuation. The authored packet
generator uses synthetic geometry; it contains no captured game assets or
guest instruction bodies. It reproduces 69 concrete input specifications:

- Short, retreating, below-axis, above-axis, and intersecting capsule sweeps.
- Parallel and out-of-bounds polygon sweeps.
- Two-plane and three-plane corners, including a ceiling.
- All four native rounding modes, an FZ/DN/sticky-status mode, and x87 TOP 7.
- Stack-page root replacement precisely at each formerly missing callback.
- Three stationary input/output alias arrangements, including partial overlap.

The guest FCW remains `0x027f` (PC53). No precision conversion, field mask, or
state repair is used.

| Evidence lane | Full-call comparisons | Callback snapshots per side |
| --- | ---: | ---: |
| Traced source bodies versus fusion | 69 | 775 |
| Exact retained objects versus fusion | 69 | 775 |
| One-record continuation overflow | 25 | 259 |
| Compile-OFF generic route | 14 | 129 |

All 177 comparisons pass. Every lane covers all 14 callback sites. Native
return-site records confirm that `85720` and `85A00` execute as the original
generic functions during continuation overflow. The enclosing `864C0` and
`170C10` remain fused in that configuration; the compile-OFF lane exercises
their complete generic route as well.

At every callback, the test checks the complete context, hashes the entire
8 MiB guest arena and both page tables, checks mapping roots, profile scope,
native FPSCR, and asserts original/current context pointer identity. Complete
context and arena bytes are compared again at full-call return, together with
all observer events, mapping tables, mutation counts, and FPSCR.

The retained objects lack usable source-line information. Their callback
sequence is checked against snapshots recorded by the signature-checked source
oracle. Only after the entire state matches does the host annotate the fixture's
`ns_site` field. This field belongs to the test observer; no guest context,
memory, or flags are repaired. Some source branches share a compiled native
callback address, so native return addresses alone cannot identify every guest
site. Native caller ownership is also checked independently.

A deliberately broken post-yield reload is rejected on callback 2 with a
complete-context mismatch. The oracle does not conceal that regression.

## Concrete missing-site witnesses

The capsule has axis `(0,0,1)`, base `(0,0,0)`, and radius `0.25`. The polygon
is a large square. These cases supplement the five previously observed sites.

| Callback | Witness |
| --- | --- |
| `85B63` | Capsule: start `(2,0,0.5)`, motion `(-1,0,0)`; intersection lies after the motion interval |
| `85B70` | Capsule: same start, motion `(1,0,0)`; intersection lies behind the interval |
| `85C63` | Capsule: start `(2,0,-1)`, motion `(-4,0,0)`; sweep is below the axis interval |
| `85C76` | Capsule: start `(2,0,2)`, motion `(-4,0,0)`; sweep is above the axis interval |
| `85839` | Polygon: start `(0,0,-1)`, motion `(1,0,0)`; parallel and behind its plane |
| `859A0` | Polygon: start `(20,0,0.5)`, motion `(1,1,0)`; outside an edge interval |
| `859B3` | Polygon: same start, motion `(0,1,0)`; parallel to an excluding edge |
| `17124D` | Two orthogonal contact planes, start `(1,1,1)`, motion `(-2,-3,-4)` |
| `1713C9` | Three planes with normals `+X,+Y,-Z`, start `(1,1,-1)`, motion `(-2,-3,4)` |

The final case needs the ceiling normal. The solver selects a negative vertical
normal in this exit path; the analogous floor corner reaches `17124D` but does
not take `1713C9`.

The complete covered set is `85839`, `85847`, `859A0`, `859B3`, `859C2`,
`85B63`, `85B70`, `85C63`, `85C76`, `8660A`, `8661D`, `1710B6`, `17124D`,
and `1713C9`. These are callback observations, not a claim of exhaustive branch
or arbitrary-input coverage.

## Reproduction and remaining scope

Generate the authored cases outside the source tree:

```sh
python tools/tests/collision_solver_yield_cases.py --out "$CASES"
python tools/prototype_collision_solver.py \
  --xbe "$OWNED_XBE" --manifest "$MANIFEST" --retained "$RETAINED" \
  --out "$PRIVATE_TRACE" --trace --inline-primitives --cases-json "$CASES"
python tools/prototype_collision_solver.py \
  --xbe "$OWNED_XBE" --manifest "$MANIFEST" --retained "$RETAINED" \
  --out "$PRIVATE_RETAINED" --production-objects --inline-primitives \
  --cases-json "$CASES" --yield-oracle "$PRIVATE_TRACE/result.json"
```

The case generator's `--subset fallback` and `--subset off` select 25 and 14
cases. Run those with `--frames 1` or `--disable`, respectively, using the same
recorded oracle. All output directories must be new and outside the repository.
Detailed commands, object hashes, callback snapshots, and negative-control
receipts remain in the private `native-solver-yield-followup/` evidence archive.

This closes the previously identified callback-coverage gap for the bounded
fixture matrix. It does not test the real scheduler, establish native stack
headroom, time firmware copies, or replace separate alias/root/REP-watch reviews.
The actor guard, packet collector, worker scheduling, and generic functions
remain unchanged. Production integration and hardware testing are separate
decisions; no build was deployed by this work.
