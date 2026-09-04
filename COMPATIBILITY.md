# Game compatibility

Xita statically recompiles original Xbox executables to run natively on the PlayStation
Vita. Each game is a separate port: the recompiler is generic, but every title needs its
own symbol map, its own set of kernel and library calls served by the runtime, and its own
shader translations. This page lists the games Xita targets and how far each one plays.

Ratings: **Playable** (start to finish, normal play), **Ingame** (loads and renders,
known problems listed), **Menus** (boots to its front end only), **Boots** (executable
starts, no usable output), **Untested**, **Broken** (does not start).

| Game | XDK build | Vita | Vita3K | Status | Details |
| --- | --- | --- | --- | --- | --- |
| Halo: Combat Evolved | 3925 | Ingame | Ingame | Menus, profiles and saves work; Blood Gulch playable at 11 to 20 fps; campaign through the cryo tutorial with a camera bug after the tube exit; three more maps load. Ad-hoc multiplayer between Vitas is the next milestone. | [docs/halo-ce.md](docs/halo-ce.md) |

| Halo 2 | 5849 | Planned | Planned | Next title after Halo: Combat Evolved is playable. Expect a larger effort: a later XDK (new D3D internals and symbol map), the Xbox's full 64 MB in use, and heavier per-pixel shading. | |

No other title has been attempted yet. Candidates after Halo 2 will be chosen by how much
of their engine goes through the calls Xita already serves.

## What a new game needs

Xita is not an emulator: a game only runs once its executable has been recompiled and
every Xbox call it makes has an implementation on the Vita. In practice a title needs:

- **XDK proximity.** The runtime implements the kernel, Direct3D 8 and DirectSound
  entry points as the 3925-era XDK exposes them, and finds them in the executable through
  a symbol database (`halo_symbols.json` for Halo). Titles built on nearby XDKs reuse
  most of that; later XDKs changed D3D internals and need new HLE work.
- **Symbols.** A symbol map for the game's statically linked XDK libraries, so the
  recompiler can replace library code with the runtime's implementations instead of
  lifting it. The XbSymbolDatabase format is the one the tooling reads.
- **Graphics within GXM's reach.** Vertex programs are translated from NV2A microcode
  and register-combiner setups from the D3D pixel-shader definitions the game submits.
  Games that write the push buffer directly, or use NV2A features with no GXM equivalent
  (some texture modes, two-sided stencil tricks), need per-title work.
- **Memory.** The Vita gives the runtime about 109 MB in extended mode; the guest gets a
  64 MB physical space plus the recompiled code and decoded textures. A game that needs
  most of the Xbox's 64 MB and large texture pools will be tight.
- **CPU.** One 444 MHz core runs the recompiled game logic; 30 fps titles that were
  CPU-bound on the Xbox's 733 MHz Pentium III will not reach full speed without the
  recompiler improvements on the roadmap.
- **Your own copy.** As with Halo, the executable and assets come from the user's disc
  image and are never distributed.

## How to report

Open a comment on the pinned "Compatibility reports" issue with:

1. Game and its XDK build if you know it (the tooling prints it when parsing the XBE).
2. Build of Xita (the date in the VPK name) and whether it's a Vita, a PS TV or Vita3K.
3. How far it gets, what went wrong, a screenshot if it's visual, and
   `ux0:data/xita/xita.log` attached (the log contains only the runtime's own output).

Do not attach executables, disc images or game assets; reports containing game data will
be removed.
