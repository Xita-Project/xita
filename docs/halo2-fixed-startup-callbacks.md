# Halo 2 fixed startup and allocator callbacks

Native161 stops at the original call through slot `461DF0` in `1C2690`,
return `1C269B`. The slot contains initializer `2D8780`. The paired original
disposal call at `1C2862` uses slot `461DF4`, containing `2D7F00`.
Both absolute indirect-call instructions are fingerprinted; neighboring
registration records are excluded.

The initializer calls original getter `1473B0`, which returns static object
`4798B0`. Its image binding is vtable `45378C`. The complete initializer
proves allocation calls at vtable offset `10h`; original pointer-replacement
helper `2D6FE0` proves release at offset `20h`. Its full fingerprint includes
all three return paths. Only those two allocator slots are added here,
along with the paired fixed callbacks. Other virtual slots and registration
callbacks retain checked dispatch.

The original allocator, reference counts, pointer replacement, initialization
flag, object construction, exception-chain handling and disposal execute as
translated code. No allocation or callback is replaced with success. The
rule checks all five fingerprints, the static object binding, and nonnull
executable `.text` targets. Synthetic fixtures cover all four selected
slots, ignored neighbors, null/noncode targets, code sections, object-binding
mismatches, all guards and unchanged input. No shared compiler/runtime,
audio or graphics semantics change.

Private proof is `lifecycle-complete/fixed-startup-guards.json`,
`fixed-slot-references.txt`, `native161-next-boundary.txt`,
`fixed-slot-bodies.txt` and `allocator-binding-audit.txt`. Native162 uses
private `fixed-startup` output. The diagnostic package embeds owned game
content and must not be distributed. The current visible result remains the
original intro; the original main menu has not been shown.

All 49 focused callback, sparse-jump, profile and LOOP tests pass.
Regeneration adds 17 reachable functions, 117 blocks and 834 instructions;
there are 12,443 generated functions and 3,825 unsupported instructions,
with the unsupported count unchanged. These metrics do not establish
runtime coverage or main-menu rendering.

Native162 executes the fixed initializer and static allocator callback,
then reaches the next original forwarding target `22C300`: caller `1472C0`,
vtable `4576A4`, return `2D87C9`. The original intro is directly visible in
`native-162-view/early-movie-middle.png`, SHA-256
`4ad9320a8899626b33ca8dd9c14ebbfe95a040f7390f8892fd5d871bb6d2285a`.
Final frame135 is black and the decoded channel state remains unchanged;
no original main menu is visible. The private transition clip records
15.4 seconds around normal Start before the controlled stop.

All 171 dependency targets pass verification after the four-job build.
Native162 ELF SHA-256 is
`1dbcd5872ae772ecbeac4f2c46c2509ef33e654b7caebebdcacc50c6d46254be`,
EBOOT `9542b9fd032b2e0c692f68f18167a9d9015e5d7c3495001d449fd476f530495c`,
trace `77c6ae79e26fc44c8be4bb7cc5c2faa58785677df1cbfabc357ac87555628563`.
Evidence is private `native-162-artifacts`, `native-milestone-162.json` and
`native-162-view`. With the owned emulator stopped and display `:111`
running, replay using `python3 preserve_fresh_cache.py native162-replay`,
`python3 capture_run.py 162-replay native-162-artifacts`, then
`python3 drive_startup.py 162-replay native-162-artifacts`. The native build
and owned emulator have stopped; the isolated display remains available.
