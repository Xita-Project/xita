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
