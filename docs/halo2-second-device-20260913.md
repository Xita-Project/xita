# Halo 2 worker startup, fonts and second device

Native attempt 37 executes the observed application worker `0x00120C30` and the
allocator callbacks at `0x0011CA20`/`0x0011CA50`. The worker entry is passed by
the pinned creator at `0x00120B0E`; the allocator uses the two-slot vtable
`0x00453308..0x00453310`. Only the experimental host-channel preparation adds
these roots, after verifying the complete executable's identity.

The generic bit-string correction in `d116eae` is now validated in the game:
Halo 2 copies and opens complete font filenames. All eight distinct font files
in the private cache match the original assets byte for byte (case-insensitive
filename comparison). The earlier incomplete cache was preserved separately
before this test. The font table retains the game's appended NUL terminator.

Application startup then constructs a second graphics device. Its command ring
is `0x03B52000` (1 MiB), canonical color allocation `0x038F8000` (2400 KiB), and
canonical depth allocation `0x037CC000` (1200 KiB). The second constructor returns
and startup enters the XPP library. The stop is an undiscovered callback
`0x0040975D`, reached through the XPP initialization dispatch with return address
`0x00408C8F`. The next task is to audit that callback dispatch and its device
initialization contract.

The first black initialization buffer remains the only presented content.
There is no visible title, menu or game geometry. Shader compilation feasibility
does not establish correct vertex bindings, viewport conversion or draws.

Fatal diagnostics now remain in the stop path if Vita3K returns briefly from
its asynchronous process-exit request. Native attempt 36 had otherwise continued
scheduler logging after the terminal diagnostic; that was not meaningful guest
progress.

| Native attempt 37 artifact | SHA-256 |
| --- | --- |
| ELF | `d19f39e54b7953a29c68b8aea52dd99ceab4e106d31e3a5df373bce51b076de0` |
| EBOOT | `bd8076be9f6562e11a019cce0de3c6c8ecbbe80011997ee7c508afecf9badbda` |
| boot.log | `c395c60b1c2baea788b6b8194d871fa254c113c008202380c7ce8911dd87f483` |

The private artifact set includes fresh device and ring snapshots, an actual
Vita3K capture, and per-file font hashes. Generated code, assets and diagnostic
packages remain outside Git and must not be distributed.
