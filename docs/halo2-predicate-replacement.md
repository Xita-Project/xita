# Halo 2 original predicate replacement

Native 170 stops at destructor `30D250`, called by `310900`, return
`31095F`. Full caller `1C2D40` shows this is an initialization setter for
the predicate at owner offset `D0h`, not a whole-owner teardown. The setter
retains the incoming object, releases the old object, updates the original
embedded-interface references and honors its final update argument.

The default constructor binds `4138DC` on the old object and `4138C8`
on its embedded predicate interface. Selected slot `4138DC` points to
the original deleting destructor. Its base destructor `30D280` restores
the original interface tables and tailcalls `DC370`; allocation metadata,
conditional base cleanup, stored size and final allocator free stay in
guest code. All tailcall and alternate-return paths remain intact.

The same initialization caller allocates a `9Ch`-byte replacement, invokes
`2DFBF0`, assigns its derived interface tables and configures its original
bit matrix with `2DF950`/`2DF990`. It then passes that object to `310900`.
The replacement's interface at offset `8h` retains vtable `4555E8`, used
by the already audited `30E4B0`/`3728C0` pair-dispatch path. Its selected
original predicate `2DF9D0` compares member identifiers and otherwise reads
the configured matrix. It writes the original decision to the caller's
output and performs `ret 0Ch`. No host predicate result is supplied.

Discovery selects exactly two slots, `4138DC` and `4555E8`. Eleven complete
fingerprints cover constructors, initialization, replacement, dispatch and
both body chains, including the predicate branch after its first return.
Targets must be nonnull executable `.text`; adjacent interfaces are not
scanned. All 60 focused callback, profile, LOOP and sparse-jump tests pass,
covering every guard, invalid targets, ignored neighbors and unchanged
synthetic input.

Regeneration adds three reachable functions, 13 blocks and 59 instructions;
unsupported instructions remain 3,792. These are translation counts, not
runtime coverage.

Private proof is in `owner-field-release/predicate-replacement-guards.json`
and `predicate-replacement-full-bodies.txt`. Native 171 uses private
`predicate-replacement` output. No shared instruction, graphics or audio
behavior changes. The diagnostic package embeds owned game content and
must not be distributed.

Native 171 completes the original predicate replacement and reaches a
later allocator callback: target `22C830`, caller `1472C0`, return `1472DF`,
vtable `457620`, object `014EC040`, ESP `005E5EC0`. The next task is to
verify that allocator's constructor and method bindings. This startup
trace does not establish execution of the newly translated bit-matrix
predicate itself.

The original Microsoft Game Studios intro is visibly present in
`native-171-view/early-movie-middle.png`, SHA-256 `b84efeae980c1350e75bbdc17c4f8b1c8c33eccfe605f1bf3bcc8aae85fd8f85`.
Map copy completes before normal Start. The transition recording lasts
31.4 seconds; final frame 135 remains black, and decoded channel/push
snapshots are unchanged. The original main menu is still absent.

| Native 171 artifact | SHA-256 |
| --- | --- |
| ELF | `cfd1c832ea71bc7545a19734d7ddc83aec6d5816a84771957339a31d3e7387c6` |
| EBOOT | `5a0e2a5d34cbb7f379d1796c8009587c91bf333fd763d2074fc994a1bb406b5b` |
| Boot trace | `09b470b853c030b6fb44b61bd2c8ce848eb1cc13347d7980198045e5c6a495aa` |

All 171 dependency targets were verified. Private replay from the private
base, with only this lab stopped: `python3 preserve_fresh_cache.py native171-replay`,
then `python3 capture_run.py 171-replay native-171-artifacts`, then
`python3 drive_startup.py 171-replay native-171-artifacts`. Archived packages
contain owned content and must not be distributed.
