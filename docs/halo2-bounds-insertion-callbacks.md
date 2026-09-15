# Halo 2 original bounds query and insertion

Native 166 stops at target `30B110`, called by `30E4B0` through vtable offset
`18h`, return `30E4FD`. Constructor `30B480` assigns vtable `4137CC` and
copies input vectors at offsets `20h`/`30h` into object offsets `60h`/`70h`.
Slot `4137E4` points to the original getter, which copies those stored
vectors into the caller's 32-byte output. Neither vectors nor output are
supplied by a host replacement.

Immediately afterward, the original caller passes that output and the
member record to offset `4h` of the object at owner offset `C4h`.
Constructor `3101C0` initializes the original bounds arguments, calls
`2E4F70`, and stores the returned object at `C4h`. The factory preserves its
original parameter handling and allocation, invokes constructor `2E1D10`,
and returns its result. That constructor binds vtable `412600`, whose
slot `412604` points to insertion method `2E24B0`.

Discovery adds exactly these two original methods. The insertion method
keeps its packed min/max encoding, original index records, output updates
and allocation behavior. No synthetic bounds, index record, callback
completion or successful return is inserted. All subsequent dispatch
continues through the existing checked runtime.

Six complete fingerprints cover the caller, bounds constructor/getter,
owner constructor, factory and index constructor, including all return
paths. Selected targets must be nonnull executable `.text`. Adjacent
vtable slots are not scanned. All 56 focused callback, profile, LOOP and
sparse-jump tests pass, covering both required targets, every guard,
invalid targets, ignored neighbors and unchanged synthetic input. Regeneration
adds five reachable functions, 61 blocks and 760 instructions, with unsupported
instructions unchanged at 3,792. These are translation counts, not runtime
coverage.

Private proof: `member-query/bounds-insert-guards.json`,
`bounds-insert-guarded-bodies.txt`, `native166-field-copy-references.json`
and `native166-index-insert.txt`. Native 167 uses private `bounds-insert`
output. No new shared instruction, audio or graphics changes are included.
The diagnostic package embeds owned game content and must not be distributed.

Native 167 verifies all 171 dependency targets and executes the original
bounds query/insertion. The next strict stop is target `30D220`, called by
`3728C0`, return `3729E1`, interface pointer `014E2E68`, ESP `005E5D84`.
The original caller is now processing the insertion's pair list and invoking
its supplied predicate interface. That callback is the next audit boundary.

The original Microsoft Game Studios intro is visibly present in
`native-167-view/early-movie-middle.png`, SHA-256
`5058f9e6868896e0efb1eef3a90b0aaf02a7c970973571acd1dee04a110eb53a`.
Map copy completes before normal Start; the transition clip lasts 37.6
seconds. Final frame 135 remains black and the decoded channel is unchanged.
The original main menu is still absent.

| Native 167 artifact | SHA-256 |
| --- | --- |
| ELF | `3684c4ca2fb0479e563ee148a0536afa39a998b3ec8e7b5c22b002ef96b46be7` |
| EBOOT | `e3185a414a2ced64aa300437435c36bbd9f2bf99a7e68680c91b38c81ea086d7` |
| Boot trace | `4847626b3bcebcd4a561267025fb1e4d3706e4d12ffed331ed55085156ef04bc` |

Private replay from the private base, with only this lab stopped:
`python3 preserve_fresh_cache.py native167-replay`, then
`python3 capture_run.py 167-replay native-167-artifacts`, then
`python3 drive_startup.py 167-replay native-167-artifacts`. Archived packages
contain owned content and must not be distributed.
