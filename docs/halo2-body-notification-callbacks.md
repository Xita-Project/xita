# Halo 2 original body notifications

Native 168 reaches `2789F0` through the listener array in `30AF20`:
return `30AF63`, vtable `45A768`, interface `014DD76C`. The original
constructor `2784C0` binds that interface at object offset `0Ch`, creates
each bounds body, and passes the interface to `315910` before inserting
the body through `30E4B0`. Registration appends the original pointer to
the body's array at offsets `3Ch`/`40h`, retaining original capacity growth.

The original add/remove dispatchers `30AF20`/`30AF80` iterate that array
in reverse, skip null entries and call listener slots zero/four. Discovery
selects exactly `45A768` and `45A76C`, pointing to `2789F0` and `278A70`.
Adjacent tables for other interfaces are not scanned.

Both original callbacks check the other member type and owner pointer,
then search its property array for key `2001h`. The add callback sets
flag `1000h` in the selected original object record; removal clears it.
The add path retains calls `A7670` and conditional `B8540`. Missing-key
and sentinel behavior, object lookup, writes, return values and caller
flow are executed by the original guest code. No property, callback result
or flag update is supplied by a host replacement.

Six complete fingerprints cover the constructor, registration, both
dispatchers and both listener bodies. Each selected target must be nonnull
executable `.text`. All 58 focused callback, profile, LOOP and sparse-jump
tests pass, including every guard, invalid targets, ignored neighbors and
unchanged synthetic input.

Regeneration adds two reachable functions, 23 blocks and 89 instructions,
with unsupported instructions unchanged at 3,792. These counts do not
measure runtime validation.

Private proof is in `pair-listeners/body-notification-guards.json` and
`body-notification-full-bodies.txt`; the earlier exploratory disassembly
windows are not boundary evidence. Native 169 uses private
`body-notification` output. No shared runtime, instruction, graphics or
audio behavior changes. The diagnostic package embeds owned game content
and must not be distributed.

Native 169 passes the original notification and reaches a later destructor:
target `30CEA0`, caller `30C1A0`, return `30C1E5`, vtable `4138C0`,
object `014E3100`, ESP `005E5E64`. The caller is replacing an owner field
and releasing its prior referenced object. This is the next audit boundary;
this trace does not demonstrate the translated removal notification.

The original Microsoft Game Studios intro is visibly present in
`native-169-view/early-movie-middle.png`, SHA-256 `ca1ba2607a2723193d820b66def59aad6ed90617ef85b2ef365aa22857e3fbce`.
Map copy completes before normal Start. The transition recording lasts
50.4 seconds; final frame 135 remains black, with unchanged decoded channel
and push snapshot. The actual original main menu is still absent.

| Native 169 artifact | SHA-256 |
| --- | --- |
| ELF | `4d42a70c02658e24c0cd4db5a7ce7a3ae9f483470716a076c8fc068f064a03ba` |
| EBOOT | `d4d771b04c45e774405cff8ffb601f8e069ec6169789dc3184258e50025bd750` |
| Boot trace | `d68c9c6dd39e879add1b3564ad2713f0040f97117320939f807e96c0d02209ea` |

All 171 dependency targets were verified. Private replay from the private
base, with only this lab stopped: `python3 preserve_fresh_cache.py native169-replay`,
then `python3 capture_run.py 169-replay native-169-artifacts`, then
`python3 drive_startup.py 169-replay native-169-artifacts`. Archived packages
contain owned content and must not be distributed.
