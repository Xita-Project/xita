# Halo 2 non-indexed quad palette state

Native83 runs the original movie quads on a freshly formatted diagnostic cache4,
then responds to Start by reading real mainmenu data into guest RAM. The entire
59,670,016-byte cached map matches the owned payload after its 2048-byte header;
only the original game's path/timestamp fields differ in the header. Execution
stops at original callback 11CC10 in 137CA0, return 137CDC. It does not follow
native81's dashboard/title-exit route. The displayed image remains black; this
is real map-loading progress, not a visible movie or main menu.

The first native82 quad had matched the pinned native73 program, constants,
validity bits and retained state except four palette descriptors. The original
allocator changed their offsets from 03B41000 to 037BB000. Unit0 uses linear
X8R8G8B8; units1..3 are disabled. None uses indexed palette lookup.

The consumer now admits differing palette descriptors only under this same
explicit non-indexed/unit-enable restriction. All descriptor bits remain stored
unchanged; reserved bits still reject. It neither maps nor reads unused palettes.
The texture pixel offset already supported per-frame relocation. Every other
retained input still matches the pinned contract, and all existing texture,
attachment, DMA, alias, shader, vertex and RGB-only commit guards remain active.
Indexed textures and additional sampled units remain unsupported even if a
reference contract were changed to match them.

Pinned [xemu texture conversion](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/texture.c)
uses palette entries for indexed I8, while its
[texture binding path](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/gl/texture.c)
skips disabled units and excludes non-indexed palettes from content hashing.
This change specializes that distinction to the already supported quad.

All 24 host executables plus timed/active runtime cases pass. Quad ASan/UBSan
passes. Synthetic cases vary every palette DMA selector/length, use unbacked
offsets and verify no additional instance reads/mappings, no state or memory
mutation at BEGIN, reserved-bit rejection, indexed/enabled-unit rejection and
an exact completed RGB copy with every destination alpha byte preserved.
The existing private shaders and generated guest C are unchanged.

| Private native83 artifact | SHA-256 |
| --- | --- |
| ELF | `386e1677b6c267c877a7d9744d7bf9900e3c3e27d0dd79c1caad6162b32ea9d1` |
| EBOOT | `02d0ddc7e619381a9a3b0ee12c9bbc7a75d242ddd31f51cbc7ea8bbc4b5d46aa` |
| VPK | `65f71c7daace612589f275a88f1e207665397625ec8c45945417336b47a68f8c` |
| boot.log | `d6750ae430ab677631824a87126f8d7f2315c85ab26a3df9b5548b76b3c4a560` |
| channel-at-stop.json | `5ed3e1de896c901b1b866c01b8fc6858a7792abde594503dacd83141dbd6f6b9` |
| last-presented-at-stop.bin | `7ae5abb8e537546742e467c54adec7af73a4b5770c69b2fad1c3b59679a2f147` |

The last framebuffer header is {960,544,3840,304}; pixels remain black. The
emulator is stopped. Private `native83-cache-mainmenu-comparison.json` records
the full-file comparison; `native-83-view` contains actual window/video captures.
Next: validate the bounded per-map callback walk and execute original callbacks.
The populated-cache raw/namespace limitation remains explicit; native83 preserved
the previous cache4 directory and let the original formatter initialize an empty
private directory. This is not repeat-boot support for populated FATX volumes.
No saves or assets outside the diagnostic lab were touched. Game-embedded VPKs,
owned bytes, caches, generated code and captures must stay private and out of Git.
