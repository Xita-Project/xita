# Campaign capture queue: omit unused coverage masks

The physical Vita booted `0.2.0-perf.4 / 2900569+`, runtime SHA-256
`318ac628ec87829f38f5bbe6d7d71a532e784fd5786d0d1856b12c19ec027556`.
This retains the earlier optimizations and adds the bounded raw vertex
comparison described in [the NEON capture note](capture-comparison-neon-20260918.md).
Halo 2 is parked at the user's request; campaign performance is the priority.

Ordinary Pillar of Autumn gameplay includes crowded combat around 5–6.5 FPS.
The one-frame draw trace deliberately stalls recording; exclude its affected
window (1.8 FPS) from performance comparisons. Reverting through the game's
**Revert to Saved** menu restores the checkpoint at `-28.66,32.52,0.62`,
direction `0.56,0.82,-0.15`. Settled samples return to about 12.7–13 FPS,
with 141–156 draws/frame. These different scenes do not demonstrate a gain
from the new comparison. The user also reports faster loading; it has not
been timed against an equivalent cache state.

One settled 156-draw window reports 77.9 ms game + 1.1 ms wait. Inclusive
owner tick/render intervals are 35.66 / 41.36 ms per frame. Draw preparation
includes 7.085 ms streams and 2.241 ms indices; capture takes 6.227 ms on the
recording side and only 351 microseconds joining over all 60 frames. These
nested/overlapping measurements must not be added. Stream preparation is
still a useful bounded target, though eliminating it alone cannot reach
the 50 ms budget for 20 FPS.

## Change

Every newly captured stream previously copied its 1,032-byte vertex coverage
record whenever a mask was supplied. The uploader only uses that record for
sparse raw inputs. Dense, small or span-incompatible inputs use full-span
validation, and packed inputs discard the mask entirely.

The queue now copies only masks that its raw uploader can use. All other
queued mask pointers become NULL; no pointer into producer scratch survives
publication. Sparse masks remain private copies. Exact vertex capture/reuse,
packed payloads, output bytes, worker ordering, frame retirement and graphics
settings remain unchanged. The existing periodic report counts masks retained
and omitted and the corresponding avoided metadata writes. It does not time
each mask or claim those bytes translate to an FPS gain.

## Validation

All six raw/packed/compact and reuse-off/on production FIFO configurations
pass normal, ASan/UBSan and ThreadSanitizer checks. New cases protect the
producer mask with `PROT_NONE` and overwrite source vertices before the parked
worker executes. They cover dense, short, span-incompatible, sparse and packed
inputs; resulting GPU bytes must match the independent snapshot, while a
sparse raw mask must remain privately owned. Existing mutation, sparse-to-full,
slot-wrap, capacity, failure and delayed-copy lifetime cases still pass.

## Hardware follow-up

`0.2.0-perf.5 / be69685+` is boot-confirmed in physical slot 1, runtime
SHA-256 `8431cfb669e360a9e36d798539456d5696e7013f8836572393cc2c6b23b831cf`.
The package changes only `game-a.self` and its boot record versus perf.4;
assets, shaders and update contract match. The incremental build changes the
capture object and four objects embedding the updated version. Earlier
optimizations remain enabled with the same graphics configuration.

The ordinary campaign menus restore New001's existing checkpoint. Marines
are visible moving through the scene. The last twelve full-tick, active
60-frame windows at the same rounded camera endpoints give:

| Observation | perf.4 | perf.5 |
| --- | ---: | ---: |
| Median displayed FPS | 12.8 | 12.8 |
| Median total frame time | 78.45 ms | 78.30 ms |
| Median draws/frame | 150 | 148.5 |
| Median stream preparation | 6.887 ms | 6.720 ms |
| Median recording-side capture | 6.054 ms | 5.919 ms |

Active NPCs vary, and these are not deterministic replays. The small timing
differences do **not** establish an FPS improvement. The new counters do confirm
3,990–4,510 KiB of unused mask writes avoided per 60 frames; no sparse masks
were needed in these selected windows. The change stays in the cumulative
build because it removes unused work and passes the ownership checks.

Reaching 20 FPS still requires a larger reduction: owner tick and render
intervals remain roughly 36 and 40 ms. Prioritize shared object/collision work
and model preparation within `5B760`, preserving the existing native helpers
and worker protections. Private captures, package receipts and the checkpoint
summary are in `2026-09-18-unified-games/ce-perf4/` and `ce-perf5/`.
