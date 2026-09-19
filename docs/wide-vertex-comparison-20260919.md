# Reducing branches in exact vertex comparison

Vertex capture and resident uploads repeatedly compare unchanged raw geometry.
The existing grouped comparator reduces NEON differences to an ARM scalar and
branches every 64 bytes. `XV_VERTEX_WIDE_COMPARE=1` optionally folds four groups
before that reduction on long inputs. It still reads and compares every byte.
It adds no hash, sampling, immutable-guest assumption or retained GPU lifetime.

For spans of at least 320 bytes, the first 64-byte group keeps its early mismatch
exit. Subsequent complete 256-byte groups combine differences with bitwise OR,
so differences cannot cancel. The original bounded group/vector/scalar tails
handle the remainder. No load extends outside the requested spans; unaligned
inputs remain supported. Short spans and non-NEON builds retain the old path.

The option defaults Off. Its strict 0/1 configuration stamp affects only the
capture and upload objects. Existing exact-reuse keys, sparse and packed paths,
mutation invalidation, worker ownership and frame retirement are unchanged.
This is independent of the persistent GPU cache, which remains disabled after
its adverse hardware result.

## Qualification

The Vita-compiled On comparator passed 28,335 cases / 85,075 calls against the
original comparator and Vita libc. These cover every input-alignment pair at
the new 320/576-byte boundaries, every mutation position at lengths 320, 321,
512, 513, 576 and 1,024, shorter boundaries, longer spans, equal pointers, zero
length and unmapped page ends. No out-of-range read or write was observed.
The Off path passed 21,997 cases / 66,061 calls. The standalone test requires
`--blocks` with `--wide`, so it cannot silently test only the unchanged helper.

| Equal span | Existing grouped instructions | Wide instructions |
| --- | ---: | ---: |
| 16 bytes | 29 | 35 |
| 64 bytes | 40 | 46 |
| 256 bytes | 109 | 115 |
| 4 KiB | 1,489 | 1,193 |
| 64 KiB | 23,569 | 18,473 |

These are dynamic ARM instruction counts, not cycles, bandwidth or FPS. The
standalone wrapper incurs extra small-span overhead, while long equal spans
use about 20–22% fewer instructions. Later mismatches can read more in-range
bytes before returning than the previous 64-byte loop. Actual workload sizes,
cache behavior and compilation context determine hardware value.

Cross-compile checks cover default/Off/On/no-op and both restoration transitions.
Off capture/upload `.text` sections match the retained perf12 objects exactly;
invalid selector values fail before compilation. The final formatted header
produces exactly the tested comparator instructions. Private results are under
`vertex-wide-compare/` and `ce-perf13/` in the unified-games workspace.

## Physical Vita follow-up

`0.2.0-perf.13 / 31dbfd5+` was uploaded, hash-verified and boot-confirmed in
slot 0. Runtime SHA256 is
`c4d08cf05db8661d6d18fe4f07c9a1179c6ae6e8c6072abc1f6ed16e1e0e7da7`.
Only the runtime and boot manifest changed from perf12. The wider comparator
is enabled, persistent vertex caching remains disabled, and earlier cumulative
options remain enabled. The source default remains Off.

The Normal New001 Pillar of Autumn save loaded through the ordinary menus,
with the same checkpoint/view and unchanged settings (544-line rendering,
texture maximum dimension 256). This is ordinary gameplay logging, with the
benchmark inactive; no Vita3K performance validation was used.

The initial last twelve complete 60-frame windows measured median 78.35 ms /
12.8 FPS, compared with perf12's settled 78.3 ms / 12.8 FPS. Vertex capture
was 6.133 ms versus 5.990 ms, preparation worker 4.757 versus 4.826 ms, and
streams 6.950 versus 6.860 ms. Median draws were 149 versus 151. These small
mixed differences do not establish a benefit or a whole-frame regression.
Reduced modeled instructions did not translate into a clear FPS gain here.
A later twelve-window capture agrees: 78.4 ms / 12.8 FPS, capture 6.131 ms,
worker 4.782 ms and 149.5 draws/frame. It is a later overlapping log from the
same run, not an independent restart trial.

The user separately reported that campaign loading feels faster with the
cumulative builds. This is useful feedback, not a measured loading-time gain
attributable to this comparator. Current map read/cache-write totals are
similar to perf12; total loading duration includes work outside those I/O
counters. No searched crash marker appeared in the captured log, but this
checkpoint observation does not qualify long-session combat stability.

Private hardware evidence is in `ce-perf13/gameplay/` in the unified-games
workspace. Keep the qualified option in the cumulative research build while
leaving its production default Off; do not advertise a measured FPS saving.
