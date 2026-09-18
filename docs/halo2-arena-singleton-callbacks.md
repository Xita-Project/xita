# Halo 2 arena and singleton creator discovery

Native 162 reaches `22C300` through arena vtable `4576A4`, selected by
original wrapper `1472C0`, return `2D87C9`. The arena wrapper forwards its
arguments to virtual slot `8h` with alignment 16. The original aligned
allocator is `22C0D0`; it retains its free-space search, alignment, allocation
records and null result when space is unavailable. Neither allocator is
replaced.

Complete fingerprints cover constructor `146A20` (both return paths),
selector `1472C0` (including its tail-jump branch after the first return),
and wrapper `22C300`. Discovery adds only arena vtable offsets `8h` and
`10h`. Other allocator operations retain checked dispatch.

The initializer then uses the original singleton resolver `2D7CA0`. Eight
21-byte CRT registration functions each prepend a 12-byte record to global
`480174`. Every registration function has an exact binding in the already
validated CRT startup array. Their records contain creator, initial null
link and destination pointer; they are not inferred from an arbitrary
range of data words.

| Registration | Node | Creator | Destination |
| --- | --- | --- | --- |
| `378AD0` | `461DE4` | `2D73A0` | `48011C` |
| `378AF0` | `461DF8` | `2D7F40` | `480148` |
| `378B10` | `461E04` | `2D8990` | `480160` |
| `378BA0` | `461E7C` | `2DA2A0` | `4801D4` |
| `378C00` | `461EE0` | `2DC150` | `4802A4` |
| `379630` | `466DD8` | `3724B0` | `484B38` |
| `379650` | `466DE4` | `3726F0` | `484B40` |
| `379670` | `466DF0` | `372790` | `484B44` |

Each registration and the complete resolver are fingerprinted. Records must
have readable aligned `.data` spans and unchanged initial bindings;
destinations must occupy complete aligned data or zero-fill image spans.
Creator targets must be nonnull executable `.text`. The original resolver
keeps its list order, destination writes and pending/retry behavior for
creators that return null. Discovery adds creator bodies without invoking
them or modifying the list; object lifetime remains original code.

All 53 focused callback, sparse-jump, profile and LOOP tests pass, covering
selected arena slots, all registrations/CRT bindings, record and destination
spans, invalid targets, ignored neighbors, guards and unchanged input.
Regeneration adds 15 reachable functions, 66 blocks and 489 instructions;
unsupported instructions remain 3,825. These counts describe translation,
not executed coverage or a visible main menu.

Private proof is under `fixed-startup`: `arena-and-singleton-guards.json`,
`singleton-registration-proof.json`, `singleton-registration-windows.txt`,
`002D7CA0-full.txt`, `allocator-forwarding-complete.txt` and
`native162-allocation-boundary.txt`. Native 163 uses private `arena-singletons`
output. Its diagnostic package embeds owned game content and must not be
distributed. No shared runtime, audio or graphics behavior changes here.

Native 163 verifies all 171 dependency targets and executes the original
arena allocation. The next strict stop is target `2D76F0`, called by
`2D7570` through vtable `411D2C`, object `014D7030`, return `2D75B2`.
This is a further original object creation boundary, not an allocator stub.

The original Microsoft Game Studios intro is visibly present in
`native-163-view/early-movie-middle.png` (SHA-256
`ab7d151aaa906d7a33f71a9325c1cff10770812702f38111aa95b1bf685eb17f`).
The original map copy completes at 59,670,016 bytes before normal Start.
The transition recording contains 18.35 seconds before the controlled
emulator stop. Final frame 135 remains black and the decoded graphics
channel is unchanged. The original main menu is not visible.

Private replay: after stopping only this lab and running
`python3 preserve_fresh_cache.py native163-replay`, run
`python3 capture_run.py 163-replay native-163-artifacts` and
`python3 drive_startup.py 163-replay native-163-artifacts` from the private
base. The archived package contains owned content and must not be distributed.

| Native 163 artifact | SHA-256 |
| --- | --- |
| ELF | `758af4c78c3be4077057ea5fe3ad192f064e86380401b00773664ae4a42c7b2f` |
| EBOOT | `ded16faf31b5a3c8e5b6041c2a1d19e75b6664322981217e8d1a680a730d30f9` |
| Boot trace | `c20f7d223b3fe146b9c95667fb50c3c4aa633b74c64ba2cf26a3b932452ab43d` |
| Decoded channel | `e5a8a87bc26bd38abfadf73074c3670eeccf55cc5c7006ba055ec0854f87b017` |
| Last presented snapshot | `20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893` |
