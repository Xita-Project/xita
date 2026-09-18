# Halo 2 member query and aggregate update callbacks

Native 165 stops at `31AAC0` from `316C90`, return `316CD5`, after original
packed min/max now runs. The caller fetches the member at input offset
`3Ch` and invokes its vtable offset `14h`. The target's original six-byte
body returns `80h`. Discovery includes that original function; no host
constant-return replacement is added.

Six complete, already reachable constructors establish vtables that share
this getter. The runtime stop does not identify which of these concrete
types supplied the member, and this change does not claim that all six types
have executed. Their exact bindings are:

| Constructor | Vtable |
| --- | --- |
| `311890` | `4143A8` |
| `311920` | `414428` |
| `3193C0` | `415130` |
| `31A6B0` | `4151B0` |
| `31A870` | `415230` |
| `31AD80` | `4152B0` |

Complete caller `316A20` walks the aggregate's existing members, loads the
same member field, and invokes offsets `8h` and `4h`. Only these two methods
and the observed getter are extracted from each constructor-bound table.
Other offsets remain checked. The full guard includes both returns and the
alternate update path after the first return.

After the getter, original insertion caller `316C90` passes its result to
aggregate offset `10h`. Constructor `3171A0` binds aggregate vtable
`414FD0`, whose slot `414FE0` is `315FE0`. That method adds the argument to
field `14h`. The complete removal caller `316000` maintains the original
member list, obtains the same query result, negates it, and invokes the
same counter method. Discovery preserves these list operations and counter
updates without substituting a host counter or changing ownership.

Five complete caller/getter fingerprints and six complete constructor
fingerprints guard the extraction. All selected targets must be nonnull
executable `.text`. The resulting nine unique roots include shared methods;
neighbors and unrelated vtable fields are not scanned. All 55 focused
callback, profile, LOOP and sparse-jump tests pass, covering every selected
slot and guard, shared-target deduplication, invalid targets, poisoned
neighbors and unchanged synthetic input. Regeneration adds nine reachable
functions, 19 blocks and 635 instructions, with unsupported instructions
unchanged at 3,792. These counts are automatic translation, not runtime
coverage.

Private proof: `packed-minmax/member-query-guards.json`,
`member-query-guarded-bodies.txt`, `native165-getter-references.json` and
`native165-counter-callers.txt`. Native 166 uses private `member-query`
output. No new shared instruction, audio or graphics changes are included.
The diagnostic package embeds owned game content and must not be distributed.

Native 166 verifies all 171 dependency targets and executes past the getter.
The next strict stop is original target `30B110`, called by `30E4B0` through
vtable offset `18h`, return `30E4FD`, object `014DD790`. The target copies two
original 16-byte fields to the caller's output; its owner binding is the next
audit task. No replacement output was supplied.

The original Microsoft Game Studios intro is visibly present in
`native-166-view/early-movie-middle.png`, SHA-256
`bb1aa01f5fa5c5704065e3d27991f3fe0a86ca9c2f7b8d0c7db8c6533d9b6edf`.
Map copying completes before normal Start; the transition clip lasts 37
seconds. Final frame 135 remains black and the decoded channel is unchanged.
The original main menu is still absent.

| Native 166 artifact | SHA-256 |
| --- | --- |
| ELF | `574eb2bc9b2c39385973415242dd4690f3023bf46a3c3498e8e5eedc699597af` |
| EBOOT | `9c943cd6943d58d14fe3e79346cf03d9002450d552c8e00e6704c61e8e3a9800` |
| Boot trace | `b001770ddd67bd6e93d7d76272904acf16682fc406dfed645ba244fff66e942e` |

Private replay from the private base, with only this lab stopped:
`python3 preserve_fresh_cache.py native166-replay`, then
`python3 capture_run.py 166-replay native-166-artifacts`, then
`python3 drive_startup.py 166-replay native-166-artifacts`. The archived package
contains owned content and must not be distributed.
