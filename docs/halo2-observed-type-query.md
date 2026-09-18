# Halo 2 original shared type query

Native 173 directly observes missing entry `9B910` from `1D6D00`, return
`1D6F55`, object `80166950`, ESP `005E5DDC`. The caller invokes vtable
offset `14h`, compares the result with seven, and selects its original
geometry-handling branch.

The exact original query is `mov eax,7; ret`. Discovery adds that observed
entry for ordinary translation, preserving the original register write,
flags and return handling. There is no host query replacement or fabricated
object type. Multiple vtables share this body; its address alone does not
identify the concrete class or justify scanning other slots.

Complete fingerprints cover the 965-byte caller and six-byte target, along
with the existing whole-image gate. The target must be executable `.text`.
All 63 focused callback, profile, LOOP and sparse-jump tests pass, including
both guards, invalid/non-title targets, missing section metadata and no
inferred vtable reads.

Regeneration adds one reachable function, one block and two instructions;
unsupported instructions remain 3,792. These are translation counts, not
runtime validation.

Private proof is in `bounds-delegate/type-query-guards.json`,
`type-query-full-bodies.txt` and `native173-type-query-references.json`.
Native 174 uses private `type-query` output. No shared instruction, graphics
or audio changes. The diagnostic package embeds owned game content and
must not be distributed.

Native 174 passes this query and reaches the next original bounds callback:
`2DE0F0` from `30DF80`, return `30DFF5`, object `80166950`, vtable
`455500`, ESP `005E5E20`. The normal Start input follows the completed
59,670,016-byte map copy. The actual Microsoft Game Studios intro is visible
in `native-174-view/early-movie-middle.png`; there is no main menu.
The final guest frame is still black (frame 135), with unchanged decoded
channel and push snapshots. This run retains the early eight-second movie
capture and terminal capture; no separate transition recording was started.

The native build passes all 171 dependency-target checks. Frozen artifacts
and hashes are in `native-174-artifacts` and `native-milestone-174.json`:

- ELF: `ee25eb08bafed6ebfda3e8565b7e04e5726989800d9e11d324f65c422b674630`
- EBOOT: `b1f7c73688f8d2f8896bb228ffb109835d6ebcd62f83af15873df45822177b61`
- Guest trace: `3eb0b94815d0aa20c6dae97f40483ecbb131b61e29a78d3fb9a95891be54bd26`

From the private evidence directory, replay the frozen build with
`python3 preserve_fresh_cache.py native174-replay`,
`python3 capture_run.py 174-replay native-174-artifacts`, then
`python3 drive_startup.py 174-replay native-174-artifacts`.
