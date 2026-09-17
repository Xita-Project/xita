# Ordered collision membership scan prototype

This private candidate reconstructs integer state for the edge-result membership
loop at guest `87120..87133`, inside the already selected `172C95 -> 171F94`
query. It is not integrated into the production generator or enabled on hardware.
`XV_QUERY_MEMBERSHIP_SCALAR` defaults to zero in the generated private unit.

The earlier four-entry/read-ahead proposal is blocked on a stronger ownership
contract. A normal result list originates on the caller's stack, but the current
query adapter does not admit a live owned span under its current captured page
table after arbitrary callback changes. The scalar candidate needs no new
immutability assumption and does not release any transaction lock.

## State and access contract

The source transformation replaces one exact interval, keeps the empty-list
branch, and retains all original guest stores and generic fallback calls.
It leaves the vertex loop, floating-point operations, solver, shared headers,
scope adapters and continuation frames unchanged.

Each scalar iteration reads its current entry. Only after a mismatch does it
increment the full unsigned 32-bit EDX, sign-extend its low 16 bits into ECX,
and read the current count. It re-resolves both accesses through the original
captured arena/page roots. Compiler memory barriers prevent hoisting the actual
page/guest reads; they are not hardware synchronization or ownership fences.
No later entry is read before the first match, and no count is read on a match.

The original INC preserves the entry CMP's unsigned carry. A local raw-carry
value tracks that backing field; it is not merely an EFLAGS approximation.
At a match, list exhaustion or original preempt frontier, the candidate restores
the full lazy SUB recipe, raw carry, and budget. `X_FLAGS` resets both overrides
while preserving the raw overflow field, exactly as the original does. All other
context fields remain unchanged. The full EDX and sign-extended ECX are retained,
including noncanonical incoming values and unsigned counter wrap.

Only a taken, nonmatching `87133` backedge decrements the budget. Expiration
publishes the full shadow context and raw registers to the original guest pointer
before calling the existing preempt callback. On return it reloads all guest
registers, flags and budget. It does not retain a count, translated host address
or target across that callback. Captured integer roots keep their original
per-frame semantics rather than being replaced with global roots.

## Bounded results

The production-equivalent current query object has `.text` SHA-256
`959993a9f34f47cf311e7424e5eff112874d4eacb4e4fe743d14dff8ab899596`.
The candidate has `.text` SHA-256
`06b9b249c7a014175c27d624ec682c014d9f17cd2fc609f5efac05f52336d0fa`.
Feature OFF preserves every allocated section and normalized relocation of the
current query object. ON reduces text from 34,140 to 33,988 bytes and retains the
2,176-byte query frame, 24-byte adapter frame and unchanged import set.

| Whole query fixture | Current | Candidate | Change |
| --- | ---: | ---: | ---: |
| Four surfaces, depth 16 | 569,336 | 542,983 | -4.63% |
| One surface, depth 16 | 214,949 | 212,498 | -1.14% |
| Combined traversal, depth 16 | 1,543,589 | 1,523,378 | -1.31% |
| Short query | 13,094 | 12,968 | -0.96% |
| Empty query | 1,098 | 1,097 | -0.09% |
| Four outside surfaces, depth 16 | 514,972 | 515,913 | +0.18% |

These are modeled ARM instruction counts for complete queries, not cycles,
frame times or FPS. All guards, spills, scope and adapter work are included.
The fourteen normal cases include small regressions in paths with little scan
work; the largest relative regression is 2 instructions on a 731-instruction
rejection path (+0.274%). The first cost gate records full PC histograms.

Fifty-nine focused whole-query cases passed against the actual retained generic
ARM object and current semantic query. They cover counts 0/1/3/4/5/255/256;
first/middle/last/miss; stale lazy-flag backing fields and overrides; exact
budget frontiers; 16/32-bit index/counter wrap; aliases between count, entry and
real stack scratch; unaligned cross-page access; captured/global root divergence;
same-table remapping at the original callback; active profiling; and all four
native rounding modes plus FZ/DN/sticky flags.

The comparison includes complete final context and 8MiB guest arena, both full
page tables, root selection, FPSCR, ordered guest writes, 5,864 original pre/post
callback snapshots per lane, and 5,612 actual scalar read events per lane.
Adversarial state is introduced only after the existing fixture's actual preempt
callback returns; no additional callback, guest entry or budget decrement is
introduced. These returning callbacks do not represent production worker aborts.
The unchanged production worker still stops after receiving the fully published
original context. Native exception-handler behavior is not newly qualified.

Two deliberately wrong compiled scalar bodies are rejected: dropping raw carry
changes context byte 64 at an observer; double-debiting the backedge budget
changes the actual observed register/flag frontier. No oracle differences are
masked or repaired.

The available Unicorn build faults in the retained original when even a no-op
global memory-read hook is enabled. Read tracing therefore uses the existing
instruction-step hook at independently reviewed real LDR sites, inspecting their
actual address operands and values without changing guest/native state. Exact
ELF hashes pin every annotation. Code and write hooks remain unchanged.

## Reproduction and next gate

`tools/prototype_query_membership_scalar.py` consumes the private retained
semantic-leaf cost directory and emits owned outputs outside the source tree.
`tools/test_query_membership_scalar.py` provides focused boundary cases,
`--normal-costs`, and `--negative-controls`. Generated game source and objects
are not committed.

Evidence is under `query-membership-block-prototype` in the private validation
tree, including `blocked-block-proposal.md`, source correlation, build receipts,
full first-gate histograms, observations and the independent review. The next
gate is separate production generator/build integration with the feature still
default OFF, final object identity checks, then a cumulative hardware trial.
