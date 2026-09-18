# Halo 2 original pool allocation

Native 171 reaches `22C830` through `1472C0`, return `1472DF`, object
`014EC040`, vtable `457620`. Initializer `146A20` invokes constructor
`22C3E0` and installs its return at `479898`. That constructor binds
`457620` and initializes the original size-class tables, free lists,
pool storage and counters.

Allocation wrapper `1472C0` calls offset `10h`; on null it retains the
original next-allocator and final-allocator fallbacks. The pool allocation
method keeps its original signed size comparisons, list reuse, page
selection, exhaustion handling and counters. Its branch for requests
above `2000h` returns null, allowing that fallback chain to execute.

The matching free wrapper `147320` recognizes pointers in the original
pool span and invokes offset `14h`. Method `22CA00` preserves original
size-class and free-list updates. Its large-size branch invokes the
existing callback at `461DDC`, whose initial target `2D6FB0` tailcalls
`1EDF40`. Discovery adds that original target; the indirect dispatch and
any later runtime pointer changes remain untouched.

Exactly three slots are selected: `457630`, `457634` and `461DDC`.
Seven complete fingerprints cover initialization, constructor, allocation
and free wrappers, both pool methods and the fixed callback body. Required
targets must be nonnull executable `.text`. Aligned allocation methods,
other adjacent vtable entries and following string data are not scanned.
No host allocator, synthetic allocation, adjusted count, changed range
check or successful-return stub is introduced.

All 61 focused callback, profile, LOOP and sparse-jump tests pass, including
every guard, invalid targets, ignored neighbors and unchanged synthetic
input. Regeneration adds three reachable functions, 30 blocks and 172
instructions, with unsupported instructions unchanged at 3,792. These
counts do not measure runtime validation. Private proof is in `predicate-replacement/pool-allocation-guards.json`
and `pool-allocation-full-bodies.txt`; guarded spans include all branches
after the first return. Native 172 uses private `pool-allocation` output.
No shared runtime, graphics or audio behavior changes. The diagnostic
package embeds owned game content and must not be distributed.

Native 172 executes the pool allocation and reaches an original bounds
callback: target `2DF5D0`, caller `30DF80`, return `30DFF5`, vtable
`455540`, object `014DB240`, ESP `005E5E90`. Its constructor binding and
the caller's subsequent temporary listener are the next audit. This trace
does not demonstrate every pool exhaustion/free branch.

The original Microsoft Game Studios intro is visibly present in
`native-172-view/early-movie-middle.png`, SHA-256 `68f5f52f22a2a8d6aba4348a7867d1221a7bc9b78fe029b51681d9f8860d207c`.
Map copy completes before normal Start. The transition recording lasts
32.4 seconds; final frame 135 remains black, and decoded channel/push
snapshots are unchanged. The first captured movie input and output match
each other, though they differ from the prior run's captured frame. No
original main menu is visible.

| Native 172 artifact | SHA-256 |
| --- | --- |
| ELF | `61a69528cc1c9e09ff6e0015e64b62ba69a26cad41cc923b7bd4758a184c681e` |
| EBOOT | `eeb5b2125958d4191885e18b08b971a58643f1d3e2784fb0204a5a52b235549c` |
| Boot trace | `265b7efcdc1044f8a098b309eea0c5fb9cddb7187bbfb9d0415a677bf9e3b1ea` |

All 171 dependency targets were verified. Private replay from the private
base, with only this lab stopped: `python3 preserve_fresh_cache.py native172-replay`,
then `python3 capture_run.py 172-replay native-172-artifacts`, then
`python3 drive_startup.py 172-replay native-172-artifacts`. Archived packages
contain owned content and must not be distributed.
