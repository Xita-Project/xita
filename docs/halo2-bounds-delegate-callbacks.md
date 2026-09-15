# Halo 2 original bounds delegation

Native 172 stops at `2DF5D0`, caller `30DF80`, return `30DFF5`, wrapper
vtable `455540`. The original eight-byte method loads the child at offset
`0Ch` and tailcalls its vtable offset `18h`, retaining the caller's arguments.

Two constructor paths establish bounded delegation chains. `1C3B70`
creates the wrapper, obtains its child through `1C3B20`/`2DF760`, and
ultimately binds the stored-bounds object created by `1EED40`. The tables
are `455540`, `456160` and `4560F8`. Separately, `246C60` binds parent
`456194`, its child interface `455F58` and wrapper `455540`, retaining
their original parent/child pointers. The three wrapper slots share
`2DF5D0`; both leaf slots share `1EF040`. These bindings do not alone
identify which concrete chain native 172 contains.

The leaf passes its original stored center/extents and caller arguments
to existing helper `1CE210`, preserving output, arithmetic and `ret 0Ch`.
No bounds, transform or successful return is supplied by a host replacement.

The caller then performs original index insertion. When its original flag
selects temporary pair collection, it constructs a stack listener bound
to `4138F0`, installs it at dispatcher offset `24h`, calls `3728C0`, then
restores the original embedded default listener. Exact added/removed
methods `30DAD0`/`30DBA0` retain original pair recording, allocation and
deduplication. Their input filtering and lifetime are unchanged.

Seven exact slots select four unique methods. Eleven complete fingerprints
cover both constructor chains, wrappers, caller/dispatcher and listeners.
Required targets must be nonnull executable `.text`; neighboring vtable
entries and following string data are not scanned. All 62 focused callback,
profile, LOOP and sparse-jump tests pass, covering every guard, invalid
targets, shared-target deduplication and unchanged synthetic input.

Regeneration adds five reachable functions, 47 blocks and 238 instructions;
unsupported instructions remain 3,792. These are translation counts, not
runtime coverage.

Private proof is in `pool-allocation/bounds-delegate-guards.json` and
`bounds-delegate-full-bodies.txt`. Native 173 uses private `bounds-delegate`
output. No shared runtime, graphics or audio changes. The diagnostic
package embeds owned game content and must not be distributed.

Native 173 executes the bounds delegation and reaches a later original
geometry query: target `9B910`, caller `1D6D00`, return `1D6F55`, object
`80166950`, ESP `005E5DDC`. The caller invokes offset `14h`, then compares
the returned type value. That original query is the next audit boundary;
this trace does not demonstrate every temporary-listener branch.

The original Microsoft Game Studios intro is visibly present in
`native-173-view/early-movie-middle.png`, SHA-256 `a4baf0679bed68bf3184d5d01ac7eea7b066ffbec31fb3f79fed9d543f48243e`.
Map copy completes before normal Start. The transition recording lasts
39.2 seconds; final frame 135 remains black, and decoded channel/push
snapshots are unchanged. The original main menu is still absent.

| Native 173 artifact | SHA-256 |
| --- | --- |
| ELF | `1a1378b3b6aabeb686d6bb30844baf1da8b9c02eed252133417e3269ef9d55f1` |
| EBOOT | `44f9fa1cd8b71bc48ef8c9d56569d6756f95f5bc4398e05bf71024cd8e9a970a` |
| Boot trace | `79f74b2d410c3ea2f6cea24c537d8f925c8efeee457e7bcf7dc0ef3c9a4c05f7` |

All 171 dependency targets were verified. Private replay from the private
base, with only this lab stopped: `python3 preserve_fresh_cache.py native173-replay`,
then `python3 capture_run.py 173-replay native-173-artifacts`, then
`python3 drive_startup.py 173-replay native-173-artifacts`. Archived packages
contain owned content and must not be distributed.
