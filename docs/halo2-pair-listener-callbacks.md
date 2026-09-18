# Halo 2 original pair predicate and listeners

Native 167 stops at `30D220`, caller `3728C0`, return `3729E1`.
Owner constructor `3101C0` binds `4138C8` at object offset `8h`;
`30E4B0` passes that embedded interface to the pair dispatcher. The original
predicate writes a true byte through its first argument, returns that pointer
in EAX, and uses `ret 0Ch`. Discovery translates this exact guest body. It
does not introduce a host acceptance stub or override a predicate result.

The dispatcher merges original pair lists and selects a listener by the
two record types. Constructor `372A40` initializes all 64 matrix entries
to an embedded listener with vtable `43E534`. The owner replaces only
entries at byte offsets `28h`, `44h` and `48h` with the object bound to
`413888`. Its original added/removed callbacks are `315E70` and `315EF0`.
Both default entries point to the already translated original `72C70`
(`ret 4`); no synthetic callback completion is installed.

For a type-2 member, those listeners adjust the original record pointer
by `-10h`, obtain the body through offset `20h`, and invoke body slots
`1Ch`/`20h`. Constructor `30B480` proves that binding: record type at body
`24h`, self pointer at `2Ch`, vtable `4137CC`. The original methods
`30B2A0`/`30B0A0` retain pair acceptance/removal, duplicate checks, array
growth and record updates. The removal listener's original tailcall and
all alternate return paths remain intact.

Seven exact slots select six unique targets. Twelve complete fingerprints
cover the constructors, predicate, dispatcher, listeners and body methods.
The `30B0A0` fingerprint includes its branch after the first return; the
`315E70` fingerprint likewise includes its alternate return path. Required
targets must be nonnull executable `.text`; adjacent slots are not scanned.
All 57 focused callback, profile, LOOP and sparse-jump tests pass, including
each guard, invalid targets, shared-target deduplication and unchanged
synthetic input.

Regeneration adds seven reachable functions, 68 blocks and 301 instructions;
unsupported instructions remain 3,792. These are translation counts, not
runtime coverage.

Private proof is in `bounds-insert/pair-listener-guards.json` and
`pair-listener-full-bodies.txt`. Native 168 uses private `pair-listeners`
output. No shared instruction, graphics or audio behavior changes in this
checkpoint. The diagnostic package embeds owned game content and must
not be distributed.

Native 168 passes the predicate and original pair-add path, then stops at
listener `2789F0`, caller `30AF20`, return `30AF63`, interface `014DD76C`,
vtable `45A768`, ESP `005E5D40`. This is the original body notification
chain reached after pair dispatch. The removal path is translated but
has not been demonstrated by this startup trace.

The original Microsoft Game Studios intro is visibly present in
`native-168-view/early-movie-middle.png`, SHA-256 `3b50b4576db3efc4758103e96a82b3dd779e9f574a09011676d70309b3b4b6ab`.
The map copy completes before normal Start. The transition clip lasts
36.002633 seconds, ending at the controlled stop. Final frame 135 remains
black, with unchanged decoded channel and push snapshot. No main menu.

| Native 168 artifact | SHA-256 |
| --- | --- |
| ELF | `2ef79df34e42e7180f3128d6bbb59bbd20fbf94a7a9b9b5ddb47a473cf554f28` |
| EBOOT | `11b8ec675a0a0afa432c19ee24e34d66985be002f579c19f7c66392eda04e2c4` |
| Boot trace | `75d420ed0a6e3101db39dede1e2284c72ff247cf51086e0de298f011ae8b24e2` |

All 171 dependency targets were verified. Private replay from the private
base, with only this lab stopped: `python3 preserve_fresh_cache.py native168-replay`,
then `python3 capture_run.py 168-replay native-168-artifacts`, then
`python3 drive_startup.py 168-replay native-168-artifacts`. Archived packages
contain owned content and must not be distributed.
