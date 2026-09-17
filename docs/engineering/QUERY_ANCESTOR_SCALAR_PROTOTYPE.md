# Ordered ancestor-query scalar prototype

This private candidate reconstructs the repeated integer ancestor membership
scan at `87F7C..87F8F` in the already fused `172C95 → 171F94 → 88110` query.
It builds on `3a070127e77df9dcf42c685cc7ed419b383aa85a`; it does not replace or
remove the existing edge membership, semantic vertex, solver, or render work.
There is no production build wiring or device enable.

The prior saved-trace audit placed about 5.9–6.4% of the ordinary depth-16
query's instructions in the ancestor scan's normal ARM path. This is the next
supported scalar opportunity after the edge scan. It does not revive the
unproved block/read-ahead proposal, x87 edge localization, context deletion,
or release of the enclosing actor transaction.

## Exact local state recipe

The original interval SHA-256 is
`8b7f876a00567e70d06aa99f883785037cf0320fa8214c7b3219d400b75c50c7`.
`tools/prototype_query_ancestor_scalar.py` rejects interval drift and external
entries into its interior. `XV_QUERY_ANCESTOR_SCALAR` defaults to zero inside
the private emitted source; the cost build explicitly enables it only for the
candidate. The disabled object exactly preserves the retained query's
allocated sections and relocations.

1. Read `[EBX]` into ECX and zero EAX exactly as before. Keep EDX's full value.
2. Read each `[ESI + EAX*4 + 0x1c]` through the captured roots. A match uses the
   earliest entry and reconstructs the exact SUB flags, override selectors,
   raw carry backing value and preempt budget before the original `87FE7` exit.
3. On a miss, retain the CMP carry, increment full unsigned EDX, sign-extend
   DX into EAX, and then read `[ESI+0x18]`. Signed comparison preserves the
   original wrapped/noncanonical counter behavior. Count is not cached.
4. Only a taken `87F8D` backedge debits the budget. On expiration, reconstruct
   all flags/registers, publish the complete shadow context to the original
   pointer, call the actual existing `xv_preempt`, and reload the complete
   callback state. Then continue at the original next entry read.

Compiler memory barriers retain the entry/count/map load ordering; these are
not hardware fences or an immutability/ownership admission. The mapping table
entry and data word are reloaded each iteration. After a callback the generated
ARM recomputes the count address from the reloaded ESI while retaining the
original captured root pointers. Guest stores, generic fallback and all FP
operations are unchanged. The state recipe does not discard stale fields.

## Whole-query cost, not standalone-loop speed

Counts below execute the actual ARM objects and the complete query, including
setup, state reconstruction, spilled values, and callbacks reached by the
normal inputs. Firmware copy bodies are modeled; their calls and byte totals
are identical between baseline and candidate in every cost row. These counts
are not ARM cycles, cache misses, hardware milliseconds, or FPS estimates.

| Complete query | Current query | Candidate | Change |
| --- | ---: | ---: | ---: |
| Depth 16, ordinary | 212498 | 207922 | −2.15% |
| Depth 16, four surfaces | 542983 | 541252 | −0.32% |
| Depth 16, outside one surface | 224713 | 218345 | −2.83% |
| Depth 16, outside four surfaces | 515913 | 508121 | −1.51% |
| Depth 16, combined 3D/2D | 1523378 | 1523602 | +0.015% |
| Depth 1, ordinary | 12968 | 12997 | +0.22% |
| Depth 1, four surfaces | 34138 | 34387 | +0.73% |
| Empty | 1097 | 1103 | +6 instructions |
| Depth 40, actual continuation overflow | 633388 | 613978 | −3.06% |

Fourteen normal cases and one overflow case were checked. The worst relative
normal regression is the short four-surface case above. The overflow case
executes 73586 counted instructions in retained `f_00087EA0` on both optimized
lanes, proving that it actually reaches the unchanged generic fallback.
The candidate text grows 72 bytes (33988 → 34060); the query frame shrinks
2176 → 2168 bytes, while the adapter remains 24 bytes. Imports are unchanged.
Register allocation changes elsewhere in the combined function are included
in these whole-call figures; loop-only savings must not be extrapolated.

## Exact-state qualification

`tools/test_query_ancestor_scalar.py` compares three lanes: the actual retained
generic ARM reference, current cumulative scalar-query object, and candidate.
Sixty targeted complete calls pass. Each lane sees 3522 identical pre/post
callback/profile observations and 5612 identical scalar table/data read events.
Checks include the entire 360-byte context, full 8 MiB guest arena, both whole
page tables, global roots, native FPSCR, ordered guest writes and exact exits.
No difference is masked or repaired.

Cases cover counts 0/1/3/4/5/255/256; first/middle/last/miss; taken-backedge
budgets −1/0/1/2/3/4/5 and ample budget; stale flags and raw overrides; full
counter wrap and low-16 sign wrap; entry/count and real guest-stack scratch
aliases; unaligned cross-page count; same-table remap and captured/global root
divergence; active profiling; all four native rounding modes and FZ/DN/sticky
flags; and depth-40 fallback. Full original-pointer observations are retained.

Adversarial inputs are changes made on return from the **actual original
87F8D preempt callback**, never by entering an arbitrary PC, inserting an
extra yield, or changing the instruction-budget mechanism. The first real
fixture miss uses an invalid plane ID. Forced-match cases change ECX to an
existing plane and redirect ESI to an exactly copied synthetic descriptor so
large list counts do not overwrite the live caller's frame. All mutation
bytes and resulting ordinary query behavior are compared. These are explicitly
returning owner-like callback tests, not worker-abort or production scheduling
tests. The ordinary and overflow cost inputs do not inject such mutations.

The first diagnostic attempt rejected its own incorrect assumption that ESI
was fixed at `0x10000`: the real selected caller put its descriptor at
`0x8fdd8`. That failed fixture is preserved as evidence. The corrected check
validates the actual world pointer and CMP state at the source-reviewed return
site. The optimization itself was unchanged. A second overflow run added
function profiling to establish actual fallback execution, not new semantics.

Two deliberately wrong, compiled candidates fail: dropping raw carry is caught
at context byte 64 by the callback oracle, and double-debiting the backedge
changes the real callback frontier. Exact ELF hashes pin all manually reviewed
read/callback annotations. Only the established code/write hooks are used;
the existing Unicorn global-read-hook defect is not reintroduced.

## Decision and remaining scope

This is a reviewable cumulative CPU candidate, with larger whole-query savings
in some ordinary/deeper cases than another sub-percent arithmetic helper.
It is not a universal improvement or a demonstrated hardware gain. The current
frame-critical query share and real list-depth distribution remain unmeasured
by these synthetic cases. No several-FPS claim is justified.

Further substantially larger scan savings would need the still-missing
short-lived list/mapping stability proof before any hoisted count, lookahead,
block scan, or membership cache. Actor concurrency additionally needs an
independent publication/transaction design; this candidate preserves the guard.
Do not expand its qualification into permission for either change. Independent
review and an exact production-object comparison remain gates before enable.
