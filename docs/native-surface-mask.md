# Visibility-mask scan candidate (53E90)

This is an uninstalled prototype, not a hardware optimization yet.

A fresh Pi profile with native material/effects/point-location enabled places
53E90 at 3.5% of the helper's sampled PCs (1,372 helper samples across 480 settled
frames). This small statistical profile identifies a lead, not Vita time saved.
The routine walks a visibility bitmask and copies selected six-byte entries and
indices into output lists. It has no draw callbacks. 54010 instead contains
ordered callbacks and material/draw work; its whole body cannot be cached.

`xk_surface_mask_scan.h` batches consecutive unset bits at 53ED0, at most through
bit 30, within the positive source bound and remaining scheduler budget. The
original loop handles set bits, the final bit, all callbacks/yields and unusual
inputs. The candidate reconstructs registers, scratch counter and lazy flags.
Scratch aliases of the mask, root or count decline. The root slot is supplied
through the original image accessor so a future hook must preserve render-view
mapping. Ordinary RAM with no concurrent mutation during the batch is required.

The pinned retained-body comparison tool leaves generated guest code in a private
output directory. Initial host ASan/UBSan and Pi ARM tests passed 768 complete
context/memory comparisons, 2,505 admitted batches and 3,000 matching yields.
Those results cover sparse/dense/empty masks, partial words, multiple words,
short/long budgets and a mask change at a yield. They do not yet prove physical
alias handling, page remapping, all yield-visible states or production ownership.
A subsequent counter-consistency guard is being checked separately.

Before integration: expand alias/remapping/yield-state tests, measure ARM cost
including short/fallback runs, establish source ownership on the scene helper,
and guard the production hook's exact body and interior entries. Then validate
against real calls before normal hardware gameplay. No FPS benefit is claimed.

Run with an owned retained shard:

```sh
python3 tools/test_surface_mask_scan.py --reference /private/recomp/code_009.c \
  --out /private/new-mask-comparison
```

## Expanded checks and first ARM cost result

Host yield-state checks now compare the entire guest context at every original
scheduler handoff. The 768 cases passed with 3,000 matching handoffs before
adding mapping/count mutations. Final-context equality alone was not used to
claim scheduler equivalence.

On the Pi, 50,000 calls per mask, each scanning 128 entries, gave these indicative
original/candidate costs (ns/call): empty 389/401; only bit 0 set 6,982/1,695;
only bit 31 set 7,027/1,210; all bits set 11,927/14,746; alternating bits
10,844/15,284; bits 0 and 16 set 7,237/2,384. Argument scratch is restored before
every call. The first cost attempt failed to restore arguments modified by the
original routine and is invalid; use pi-result-corrected.txt only. These costs
include common fixture setup and are not Vita frame-time predictions.

Dense/alternating masks regress significantly. Do not deploy this candidate as
is. Next establish actual mask density/run lengths and improve early rejection
or word-level admission before considering production integration.

## Actual a30 mask distribution

A separate Pi whole-game diagnostic counted each original outer-loop mask word
without enabling the candidate. The settled portion after frame 1800 contained
487,424 words in 119 complete groups: 79.565% empty, 7.136% fully set, and 2.162%
with one to four bits set. Empty words already use the original fast skip; the
remaining mixed masks require run-pattern evidence, not just population counts.
Private surface-mask-profile/build.py builds the diagnostic from the retained
shard; its summary.json and codex-mask-density-20260925.log preserve the results.

The final early-rejection fixture passed host ASan/UBSan and Pi ARM: 768 complete
context/memory and yield-context comparisons, 1,647 batches, 2,148 yields, plus
three explicit scratch aliases (root slot, bound, and mask via remapped page).
Cases include a mask-page relocation and a count change at a handoff.

Early rejection still regresses dense masks on ARM (10.38→13.62 us per synthetic
128-entry call) and alternating masks (9.80→11.46 us). Sparse examples improve
roughly 10.2→1.6 us. Results are placement/cache-sensitive and not hardware FPS.
The next candidate should select eligible words once before their inner loop,
not repeatedly pay full admission on each dense bit. Production remains unchanged.

## Once-per-word admission result

The comparison generator now selects words with at most eight set bits at
53EB9. This local eligibility flag is only a performance hint: the helper still
reloads and checks live inputs after every original handoff. Host sanitizers
and Pi ARM passed the expanded suite (768 cases, 1,511 batches, 2,148 handoffs,
plus the alias declines).

Pi fixture costs in us/call: bit 0 only 4.82→1.31; bit 31 only 4.84→1.09;
bits 0+16 5.15→1.97; fully set 9.03→9.79; alternating 7.16→7.57; empty
0.202→0.218. The per-word hint reduces fallback overhead but does not eliminate
it. Absolute values vary across runs; these are paired synthetic costs, not
whole-game or hardware gains.

Parked for now. Most actual words already take the original empty-word skip,
and dense/mixed cases still pay overhead. Do not install the prototype by
default or count it among verified speedups. A real-trace replay showing a net
benefit, plus production ownership validation, is still required. Investigate
larger preparation/recording reductions before spending another hardware build
on this candidate.
