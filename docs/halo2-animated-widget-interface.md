# Halo 2 original animated widget interfaces

Native 175 reaches a missing child update at `2B0328` from `22E391`,
return `22E3AB`, object `82537670`, vtable `45AD60`, ESP `005E5F84`.
The original parent walk follows child `14h`, sibling `18h`, and invokes
each child's virtual `0Ch` update.

Constructor `2B01EB` installs table `45AD60`, retains its descriptor at
`70h`, initializes timing from the original `54D5B8` clock and selects
original bitmap/frame data. The update calls the base child walk, then
uses that descriptor and clock to update frame selection, rectangle and
scroll offsets. These values remain entirely owned by the original guest.
Its `10h` draw slot reaches the original `2B0540` draw selection; this is
ordinary translation, not a replacement drawing routine.

Adjacent table `45ADA8` is separately bound by constructor `2B0B5E`,
which calls the same base constructor with kind seven (the observed
constructor uses kind eight). Both tables contain a 17-method executable
prefix followed by a zero word at `45ADA4` / `45ADEC`. Discovery includes
only those prefixes, preserving the zeros and leaving the following
interface untouched. It does not infer that the second kind has already
executed in the observed run. Original destructor, update and draw bodies
retain their lifetime and control-flow behavior.

Complete fingerprints cover both constructors, the child walk and the
observed update, with the existing whole-image gate. Every included target
must be executable title `.text`. All 65 focused callback, profile, LOOP
and sparse-jump tests pass, including both null boundaries, invalid targets,
missing section metadata and unchanged neighboring slots.

Regeneration adds 22 reachable functions, 211 blocks and 1,832 instructions;
unsupported instructions remain 3,792. Translation counts do not establish
execution or rendering compatibility. No graphics, sound or shared runtime
behavior changes are included.

Private proof: `two-point-bounds/widget-update-guards.json`,
`widget-update-full-bodies.txt`, `widget-update-references.json`.
Native 176 executes past the animated widget update, then stops at another
original widget setup callback: `22F1F7` from `234EBC`, return `234EEC`, object
`82537E90`, vtable `4587D0`, ESP `005E5F80`. This does not establish that
all newly translated methods executed. The complete 59,670,016-byte map
copy precedes normal Start. The actual Microsoft Game Studios intro is
visible in `native-176-view/early-movie-middle.png`, and the transition clip
contains 58.3 seconds. No main menu appears. The final frame remains black
(frame 135), with unchanged decoded channel and push snapshots.

All 171 native dependency targets pass verification. Frozen artifacts and
hashes are in `native-176-artifacts` and `native-milestone-176.json`:

- ELF: `f80f0cf8c1bae00b5f6418aad663482e2d4be7aa54e397f57fea8a6932a6b243`
- EBOOT: `174b07bb4c00f93b764e14d172a2990cf31c253686d7293c00c08f84c92a6bce`
- Guest trace: `3f1a7dec570bfb6dddafdd54e4cb27002fca51cbc82ae1e1655ea07ac26fd845`

From the private directory, replay the frozen build with
`python3 preserve_fresh_cache.py native176-replay`,
`python3 capture_run.py 176-replay native-176-artifacts`, then
`python3 drive_startup.py 176-replay native-176-artifacts`.

The diagnostic package embeds owned game content and must not be distributed.
