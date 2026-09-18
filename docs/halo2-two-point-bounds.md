# Halo 2 original two-point bounds callback

Native 174 observes missing callback `2DE0F0` from `30DF80`, return
`30DFF5`, object `80166950`, vtable `455500`, ESP `005E5E20`.
The caller dispatches offset `18h` with the original transform, tolerance
and output pointer.

The map binding walks `1C2390` and `1EA9C0` both install `455500` at
record offset `20h`. They initialize the original metadata and preserve
stored point/margin data. Neither the observed table nor the shared body
identifies which binding walk created this particular runtime object.
Only the observed slot `455518` is added to ordinary translation.

The original 178-byte callback transforms the two vectors at object
`10h` and `20h` using the supplied matrix. It combines the supplied
tolerance with object `0Ch`, computes lane minimum/maximum of the
transformed points, then expands them by that margin. It retains its
original x87/SSE arithmetic, aligned temporary stack and `ret 12` ABI.
There is no host bounds result or arithmetic replacement.

Four complete fingerprints cover the caller, both binding walks and
callback, in addition to the existing whole-image gate. The selected
target must be executable title `.text`. All 64 focused callback,
profile, LOOP and sparse-jump tests pass, including every fingerprint,
invalid targets, missing section metadata and untouched neighboring slots.

Regeneration adds one reachable function, three blocks and 74 instructions;
unsupported instructions remain 3,792. Translation counts are not proof
that those functions have executed. No shared instruction, graphics or
audio changes are included.

Private proof: `type-query/two-point-bounds-guards.json`,
`two-point-bounds-full-bodies.txt`, `bounds-shape-references.json`.
Native 175 passes the two-point bounds callback and reaches `2B0328`
from `22E391`, return `22E3AB`, object `82537670`, vtable `45AD60`,
ESP `005E5F84`. Normal Start follows the completed 59,670,016-byte map
copy. The original Microsoft Game Studios intro is visibly confirmed in
`native-175-view/early-movie-middle.png`; the separate transition recording
contains 20.5 seconds. No main menu appears. The final frame remains black
(frame 135), with unchanged channel and push snapshots.

All 171 native dependency targets are verified. Frozen artifacts and hashes
are in `native-175-artifacts` and `native-milestone-175.json`:

- ELF: `c1d9b1b33585ae28bf6aad97bd8f367095ecc536c73e83e92201096502f033de`
- EBOOT: `e3283bf0861a73049f4ee3fb6df3988f1df4b3ed92953b1c95176ad4d021223a`
- Guest trace: `a692781931d857bdf274fd4243b7c36208032d43428069de7d5203d169bc1d39`

From the private directory, replay the frozen build with
`python3 preserve_fresh_cache.py native175-replay`,
`python3 capture_run.py 175-replay native-175-artifacts`, then
`python3 drive_startup.py 175-replay native-175-artifacts`.

The diagnostic package embeds owned game content and must not be distributed.
