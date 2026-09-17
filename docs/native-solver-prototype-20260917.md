# Private packet-to-motion solver prototype

The complete `172CB8 → 170C10` call is a useful next restructuring boundary.
The private prototype reduces modeled ARM instructions for moving collision
packets while preserving the actor guard, original guest precision, and generic
functions. It is **not integrated or ready to enable on hardware**.

This work starts at `8322ba1`. All eight retained optimizations remain unchanged.
The generator and synthetic fixture are authored source; generated guest bodies,
the owned executable, linked ARM programs, and detailed receipts stay outside
the repository.

## Implementation

`tools/prototype_collision_solver.py` verifies the owned executable hash, all
12 caller/solver signatures, the complete direct-call closure, and exact source
equality with the retained build. Optimized Python is rejected. It transforms
11 complete functions into a separate, nonrecursive continuation machine:

`170C10`, `170980`, `1709D0`, `B2D80`, `B1680`, `864C0`, `85D10`,
`11120`, `85A00`, `111A0`, and `85720`.

Guest GPRs remain local between internal calls. A complete 360-byte shadow
context retains every other field, including stale x87 slots and lazy flags.
Original guest memory operations retain their order. The prototype keeps
original double/PC53 behavior; it does not replace operations with float32 or
discard supposedly dead state.

Each internal call saves its captured integer/image mapping roots. Guest POP
and x87 helpers continue to use their original global-root behavior. Before a
yield, trap, generic fallback, or either runtime REP MOVS boundary, the adapter
publishes all fields to the original context pointer. It reloads all fields
after the callback. The private pointer is never given to those observers.
Motion profiling remains at its original outer scope with the original pointer;
the root mapping snapshot is taken before the profiling callback, as before.

Four 16-byte native continuation records bound internal calls. If full, the
original child runs at that exact call, preserving earlier writes and the
current result. It does not restart the query. The generic functions cannot
re-enter this specialized adapter. The emitted adapter defaults OFF and accepts
only the explicit `172CB8` caller; there is no production hook.

The first fused implementation was rejected on actual retained-object costs:
capsules, polygons, and mixed packets regressed. The corrected version uses
`--inline-primitives` to force unchanged small x87/flag primitives inline only
inside the candidate translation unit. This avoids a compiler outlining change
caused by the larger function. Generic headers and object files stay unchanged.

## Whole-call results

Both sides execute the real caller suffix, solver, visitors, result writes, and
caller epilogue. Reference execution links the exact retained `code_000`,
`code_013`, `code_016`, and `code_028` objects. These are instruction counts, not
cycle measurements or hardware FPS results.

| Packet | Retained instructions | Candidate instructions | Change |
| --- | ---: | ---: | ---: |
| Two spheres | 10,209 | 9,238 | −9.51% |
| Two spheres, active profile fixture | 14,659 | 13,690 | −6.61% |
| Two capsules | 6,976 | 6,101 | −12.54% |
| Two polygons | 12,232 | 10,403 | −14.95% |
| Two of each | 43,708 | 37,774 | −13.58% |
| Sixteen of each | 298,592 | 256,552 | −14.08% |
| 256 of each | 4,668,032 | 4,007,032 | −14.16% |
| Stationary early exit | 656 | 700 | +6.71% |

Extra firmware copy traffic is 720 bytes for the tested capsule/early-exit
paths, 2,160 for spheres/polygons, and 3,600 for mixed packets, plus 720 bytes
per yield or generic fallback. The runner counts these bytes separately;
firmware copy execution time is **not included** in the instruction totals.

The fused function has a 1,928-byte compiler-reported native frame; the adapter
has 32 bytes. Observed whole-call peak stack is 1,984–2,264 bytes versus
376–440 for the moving reference cases. This excludes unmodeled firmware and
real scheduler/profiler stack use. Production owner fibers have 32 KiB native
stacks; object workers have 512 KiB. The query and solver frames are sequential,
not additive, under the outer caller. Complete owner high-water and production
headroom have not been established. The later production-boundary review measured
an isolated solver peak of 2,424 bytes through the actual terminal worker stop.

The fused body is 73,530 bytes plus a 110-byte adapter. Although smaller than the
77,930-byte sum of the generic closure, both versions remain present: integration
would add about 74 KiB of code. Instruction-cache effects remain unmeasured.

## Correctness evidence and limits

There are 63 passing cases in the final bounded set: 41 instrumented full calls,
14 calls against exact retained objects, five forced one-record continuation
fallbacks, two compile-OFF cases, and one strengthened mapping/POP witness.
Comparisons include the entire context, 8 MiB guest arena, page-table roots and
contents, full FPSCR, and observer event sequences. Observers assert original
context identity and record context bytes and relevant guest stack bytes.

Cases include active profiling, changed lazy flags and x87 TOP at a yield,
replacement page-table roots, output/stack remapping, changed saved EBP,
rounding/control modes, stale NaN payloads, exceptional inputs, and larger
packets. Negative controls reject a leaked shadow pointer, a missing reload
after yield, and a captured-root POP. The first POP probe did not distinguish
the bug; changing the remapped saved EBP made it a valid witness. Its final
context still matched, but the intermediate observation differed.

These fixtures observe yield callbacks at only **five of fourteen** original sites:
`85847`, `859C2`, `8660A`, `8661D`, and `1710B6`. Remaining sites are
`85839`, `859A0`, `859B3`, `85B63`, `85B70`, `85C63`, `85C76`, `17124D`, and
`1713C9`. The recorded mask covers callbacks after budget expiration; it does
not establish which other branch sites executed without a callback.

| Unobserved yield site | Owning function | Original branch target/condition |
| --- | --- | --- |
| `85839` | `85720` | `8581F`, parity clear |
| `859A0` | `85720` | `8581F`, zero set |
| `859B3` | `85720` | `8581F`, parity clear |
| `85B63` | `85A00` | `85B2B`, zero set |
| `85B70` | `85A00` | `85B2B`, parity clear |
| `85C63` | `85A00` | `85B2D`, parity clear |
| `85C76` | `85A00` | `85B2D`, zero set |
| `17124D` | `170C10` | `171220`, signed less |
| `1713C9` | `170C10` | `171363`, parity clear |

Multi-contact sliding and polygon edge/vertex cases need concrete fixtures
before promotion. Arbitrary physical aliases, replaced image/arena
roots, diagnostic-enabled REP MOVS, and actual scheduler/profile behavior have
not been qualified. The fixture provides a VFP square root and modeled firmware
copies; it is not a full Vita libc/runtime execution. Traps retain publication
structurally but were not reached by the successful fixtures.

The fixture packets are authored synthetic geometry, not captures from gameplay:
alternating intersecting/distant spheres, axis-aligned capsules, and a large
four-vertex polygon. The executed reference is the real retained guest solver,
with a signature-checked re-emission of its actual caller suffix. Instrumented
runs recompile those same source bodies to add observer tracing; retained-object
cost runs do not modify the generic objects. These are distinct evidence lanes.

The outer actor transaction, packet collection, and worker scheduling are
unchanged and not executed by this solver fixture. The earlier unsafe unlock
experiment is not part of this work. Any production proposal must hook only
the matched caller, retain the generic fallback, account for stationary exits
and stack/code-size costs, and complete these observation cases first.

## Reproduction

Use Python with the repository's recompiler dependencies and Unicorn, plus the
Vita ARM compiler. `RETAINED` is the retained build directory containing both
`recomp/code_*.c` and `build/recomp/code_*.o`; `PRIVATE_OUT` must be a new
directory outside the source tree. The tool refuses unsupported game images.

```sh
python tools/prototype_collision_solver.py \
  --xbe "$OWNED_XBE" --manifest "$MANIFEST" \
  --retained "$RETAINED" --out "$PRIVATE_OUT" \
  --production-objects --inline-primitives \
  --spec 2,1,100000,0,0 --spec 2,4,100000,4,0
```

Use `--trace` instead of `--production-objects` for observer injection, and
`--frames 1` to force original-child continuation fallback. `--disable` tests
the compile-OFF path. Specifications are `count,kind,budget,variant,fpscr`;
the authored fixture defines the variant bits and kinds 1–4. Counts must remain
within the original 256-shape capacity. Detailed private receipts accompany
this prototype under `native-solver-prototype/` in the local validation archive.

The earlier motion samples attributed roughly 17% of the inclusive collection
wrapper to this solver. That does not make a 14% instruction reduction a 14%
frame-time reduction. Current display waits, simulation ticks per displayed
frame, and other collection work still matter; no FPS improvement is claimed.
