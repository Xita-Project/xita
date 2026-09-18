# Halo 2 original owner-field release

Native 169 reaches destructor `30CEA0` from `30C1A0`, return `30C1E5`,
object `014E3100`, vtable `4138C0`. Constructor `3101C0` allocates the
original eight-byte object, stores its size and reference count, binds
`4138C0`, and installs it at owner offset `250h`.

The replacement routine increments the incoming object's reference count,
decrements the previous object's count and preserves its original deferred
release check. When that check permits deletion, it passes flag one to
the old object's slot zero. The destructor calls the original base
destructor `318970`, conditionally frees through the original allocator
using the stored size and unchanged arguments, returns the object address
in EAX, and performs `ret 4`. Reference counts, object lifetime and free
behavior are not replaced by host code.

Discovery selects only slot `4138C0`. Four full fingerprints cover the
owner constructor, replacement routine, deleting destructor and base
destructor. The replacement fingerprint includes the path after its first
return. Other tables also referencing the same destructor do not establish
the incoming object's type; discovery makes no such inference. Neighboring
predicate slots remain outside this change.

All 59 focused callback, profile, LOOP and sparse-jump tests pass, including
every guard, invalid targets, ignored neighbors and unchanged synthetic
input. Regeneration adds one reachable function, three blocks and 18
instructions; unsupported instructions remain 3,792. Translation counts
are not runtime coverage. Private proof is in `body-notification/owner-release-guards.json`
and `owner-release-full-bodies.txt`. Native 170 uses private
`owner-field-release` output. No shared runtime or rendering changes.
The diagnostic package embeds owned game content and must not be distributed.
Native 170 executes the prior-field destructor and reaches original cleanup
`310900`. Its next strict stop is target `30D250`, return `31095F`, object
`014E2E60`, ESP `005E5E60`. The subsequent full caller audit identifies `310900` as a predicate
replacement setter for owner field `D0h`, invoked during initialization
by `1C2D40`. It is not a whole-owner teardown. See
[the predicate replacement audit](halo2-predicate-replacement.md).

The original Microsoft Game Studios intro is visibly present in
`native-170-view/early-movie-middle.png`, SHA-256 `ae39365d0d19dac92fa92ff4dbd90ef2fc7fe62632a535dd5ceeb1a000281720`.
Map copy completes before normal Start. The transition recording lasts
27.9 seconds; final frame 135 remains black, and decoded channel/push
snapshots are unchanged. No original main menu is visible.

| Native 170 artifact | SHA-256 |
| --- | --- |
| ELF | `8f1328d6c85e024e97f2b9bd033a7c966ad18a1d706b57ce10d7f1b20ca4db93` |
| EBOOT | `795bdb837f46f6a5d2c48026dee88d1f4e9479d5aa534c759795bba7c7b81f1d` |
| Boot trace | `57bb67ed933e9b4632e29339588331520453564f230e4187f0cccbbb89c9acc0` |

All 171 dependency targets were verified. Private replay from the private
base, with only this lab stopped: `python3 preserve_fresh_cache.py native170-replay`,
then `python3 capture_run.py 170-replay native-170-artifacts`, then
`python3 drive_startup.py 170-replay native-170-artifacts`. Archived packages
contain owned content and must not be distributed.
