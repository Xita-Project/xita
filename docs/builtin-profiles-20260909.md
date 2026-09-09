# Built-in multiplayer profiles — September 9

A tester reported “Unable to load saved game file” when starting multiplayer.
The September 8 vertex-comparison package reproduces the same error for both
**Default** and **Inverted** with a fresh save directory in Vita3K. The game maps
are present. This failure happens during profile selection, before map loading.

## Cause and change

Halo's generated save index contains two built-in profile entries whose valid
flag is zero. The previous compatibility fix handled game-variant entries only.
The profile menu therefore rejects these entries. Reads now normalize the valid
flag for the two exact built-in cache paths, with the expected profile type and
built-in marker. Other player-profile entries are unchanged.

There is a second problem in the original profile reader: the old signature
stub leaves scratch bytes untouched. A signature mismatch sends the loader to
its default-preferences fallback, which can lose the Inverted setting. The
runtime now computes the title HMAC for the two recognized, unnamed built-in
48-byte preference records, alongside the existing 104-byte game-variant path.
Named profiles and campaign checkpoints keep their existing signing behavior.

On a complete 512-byte read from offset zero of either generated cache file,
old built-in signatures are repaired in the guest copy. The file on disk is
unchanged. Recognition checks the built-in identifier, empty name, supported
preset/sensitivity/inversion fields and reserved bytes. Other paths, partial
reads, unexpected records and named saves are excluded. Index and signature
repairs support guest data that crosses noncontiguous memory pages.

## Validation

The full host runtime suite passes. Tests through Halo's original generated
variant and profile readers cover both built-in presets, dirty scratch memory,
direct signature generation, old cache recovery, byte-for-byte preservation of preferences,
callee-saved registers and the stack. HMACs match an independent Python result.
The existing custom-variant and named-profile signing ABI checks still pass.

File I/O tests exercise both cache filenames, wrong filenames, user saves,
partial/short/offset reads, index type/default flags and an index valid byte on
a separate guest page. ASan and UBSan pass for the reader and file I/O tests.
These host checks do not substitute for a physical Vita test.

The native build passes, with all 81 checked runtime source/header inputs
matching source. The package contains 1,585 payloads; changes are limited to the
executable, three LiveArea files and the reference notice already tracked on
main. The installed bubble icon is preserved.

On the final VPK, an isolated Vita3K instance with a fresh save directory reaches
Battle Creek gameplay through the normal Split Screen → Enlisted Players menu
with both Default and Inverted. A newly created named campaign profile also
loads the Pillar of Autumn, skips the opening cinematic to the cryo bay and
returns to the main menu through Save and Quit. These are bounded startup and
menu checks, not campaign completion or long-session stability tests. Original
cache repair and preference preservation are also covered by the host tests.
The emulator's 20 FPS cap does not measure physical Vita performance.

The package is `xita-livearea-profiles-20260909.vpk`, SHA-256
`d71608b25c9543f999d41acb7485a051dffcc8d725aa3ae5a4a582cf5b9ea118`.
Install it over Xita using VitaShell; no game-data regeneration or save deletion
is needed. Physical Vita confirmation of the profile fix and LiveArea remains
pending. This update has no measured hardware FPS gain and does not clear the
previous driving or rocket/death GPU crashes.

## LiveArea update

The same update replaces the older LiveArea background and launch-gate image
with a dark green layout. The title and footer leave room for the Vita's Start
control. The existing dashboard and in-game panel retain their appearance.

Editable SVGs are in `sce_sys/livearea/source/`. Run
`python3 tools/build_livearea.py` with librsvg (`rsvg-convert`), Pillow and
DejaVu Sans installed to regenerate the 840×500 background and 280×158 gate.
The packaged assets are indexed PNGs; template content revision advances to 2.
The SVG sources are excluded from the VPK by the existing contents-directory
packaging boundary.
