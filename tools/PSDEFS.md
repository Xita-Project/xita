# Offline PSDEF investigation: blocked on runtime state

Byte-identical reproduction of the existing captures is not a function of a
material tag alone. Investigation stopped at the requested impossibility
condition. `psdefs_from_map.py` is a standard-library-only diagnostic, **not a
completed material-pass compiler**. It writes no definitions and exits 2 for the
blocker (1 for invalid input). It deliberately does not print a misleading
`N/N reproduced` result.

## Reproduce the evidence

```sh
python3 tools/psdefs_from_map.py \
  ~/.local/share/Vita3K/Vita3K/ux0/data/xita/save/cache/cache000.map \
  --check shaders/psdefs/ --list-groups
```

Repeat with cache002 (ui) and cache003 (bloodgulch). `--list-groups` lists every
captured hash, grouped by identical structures after clearing only inactive
combiner instructions and constant colours. These groups are diagnostic, not
claims of semantic equivalence or map membership.

Observed on this checkout:

| Cache | Tags | senv | soso | sotr | schi | bitm |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| cache000 (a10) | 3357 | 45 | 127 | 39 | 27 | 485 |
| cache002 (ui) | 983 | 0 | 10 | 3 | 4 | 195 |
| cache003 (bloodgulch) | 1806 | 14 | 106 | 26 | 20 | 368 |

All three resolve the index's scenario id to a `scnr` entry. The audit reads the
index, not a scenario dependency traversal; index membership does not establish
that a tag was rendered. Cache magic is the little-endian fourcc `head` (on-disk
bytes `daeh`), version 5. Address translation follows the supplied index-base
formula. Disc-compressed maps are not supported. Version validation is not build
3925 certification; these measurements use the supplied local caches.

The same accumulated reference directory contains 571 definitions. All 571
filenames match the actual runtime hash. 539 have nonzero inactive combiner
instructions. Clearing those instructions and constant colours leaves 191
distinct structures. Even retaining active constant colours while clearing
inactive stage fields leaves multiple captures with identical active fields.

## The hash and capture are different from the proposed premise

The authoritative hash is `psdef_hash` in `recomp/kernel/xd3d.c`, around line 542.
`ps_pipeline.py` trusts the logged hash; the parser and generator describe the
layout but do not implement this hash.

The runtime uses bytewise FNV-1a, initial value 2166136261, multiplier 16777619,
32-bit wraparound. It visits the 240-byte struct **except** `[0x28,0x68)` and
`[0xAC,0xB4)`, the constant colours. It does not exclude inactive stages.
Hash equality therefore does not imply identical `.bin` contents.

`psdef_load` copies the submitted definition into `ps_shadow`. The push helper
patches individual fields via `ps_method_to_def` (the default method handler
around line 461). `xd3d_ps_sync` hashes and logs the resulting shadow, including
fields not overwritten by the current pass. This is not necessarily the
original definition submitted to SetPixelShaderProgram. The logging deduplicates
by hash, and `ps_pipeline.py` writes whichever logged snapshot it processes last
for that filename. `pairs.txt` records vertex/fragment hashes, not map, tag,
material, pass, time, or preceding draw history.

## Exactly what tag data cannot determine

| Field | Byte offset | Missing input |
| --- | --- | --- |
| `PSAlphaInputs[i]` | `0x00 + 4*i` | For `i >= (PSCombinerCount & 0xff)`, retained scratch/shadow contents from previous submissions or register writes |
| `PSAlphaOutputs[i]` | `0x68 + 4*i` | Same history dependency |
| `PSRGBInputs[i]` | `0x88 + 4*i` | Same history dependency |
| `PSRGBOutputs[i]` | `0xB4 + 4*i` | Same history dependency |
| `PSConstant0[i]`, `PSConstant1[i]` | `0x28 + 4*i`, `0x48 + 4*i` | Retained inactive constants; for runtime-varying active constants, the state/time at the captured draw and exact conversion/patch logic |
| `PSFinalCombinerConstant0/1` | `0xAC`, `0xB0` | The chosen runtime snapshot's constant values, when dynamically patched |

This does not mean every constant is dynamic: static tag colours can be derived.
It means the exact recorded snapshot cannot be selected from static tag values
alone. Shader tags also support animation and object-driven sources; deriving
their endpoints does not supply the runtime evaluation inputs. No claim is made
that all remaining fields have been reverse engineered. In particular, texture
units left enabled by prior passes and final-combiner state need a write-history
audit before they can safely be canonicalized.

A concrete witness is `1037F25B` versus `1698F060`. Both declare two stages
(`PSCombinerCount = 0x00011002`), have identical active stage fields, active
constants, final combiner, texture modes and mappings. Differences include:

| Field | 1037F25B | 1698F060 |
| --- | --- | --- |
| `PSAlphaInputs[2]` | `0x00000000` | `0x1C140000` |
| `PSAlphaInputs[3]` | `0x12201120` | `0x00000000` |
| `PSRGBInputs[2]` | `0x310C1101` | `0x0C140000` |
| `PSRGBInputs[3]` | `0x0C200120` | `0x010C0000` |
| `PSRGBInputs[4]` | `0x2C0D0C0B` | `0x0C050000` |

All their differing instructions are inactive. They belong to a group of 23
captures sharing the same active structure and active constants. A tag template
cannot choose among these historical identities. Enumerating arbitrary tails
would not establish which were drawn, and zeroing the tails changes the runtime
hash. The source's persistent-shadow behavior supplies the mechanism for the
observed differences; the captures alone do not identify the predecessor pass.

## Pass semantics established, and limits of the mapping

The two-stage witness above computes `r0.rgb = t0.rgb * v0.a`, keeps `t0.a`, and
uses the final combiner to output r0. This identifies an operation sequence,
not a shader tag or unique material pass.

`1D50F9CA` and `316344A9` both show a bump/reflection-shaped pass:

1. Stage 0 writes `t0.rgb = dot(expand(t0.rgb), expand(t3.rgb))` and
   `r0.rgb = t2.rgb * c0.rgb`.
2. Stage 1 writes `r0.a = (1 - v0.a) + v0.a * t0.b`.
3. The final combiner adds `t2.rgb * (r0.a * final_c0.rgb)` to r0 and emits t0.a.

Their texture modes are PROJECT2D, PROJECT2D, CUBEMAP, CUBEMAP. Thus even a match
to the supplied dot-product idiom does not prove t2 is a lightmap in that
capture. The proposed environment base-pass formula
`lerp(t2, t1, t2.a) * t0 * 2 * t3` remains a supplied template hypothesis here;
no map-tag-to-exact-hash mapping was established before the blocker.

The consulted Reclaimers Library definitions explain the relevant tag controls:
[environment](https://c20.reclaimers.net/h1/tags/shader/shader_environment/),
[model](https://c20.reclaimers.net/h1/tags/shader/shader_model/),
[generic](https://c20.reclaimers.net/h1/tags/shader/shader_transparent_generic/),
[Chicago](https://c20.reclaimers.net/h1/tags/shader/shader_transparent_chicago/),
and [bitmap](https://c20.reclaimers.net/h1/tags/bitmap/).
These describe material inputs, not the prior contents of Xita's shader shadow.
Bitmap type can help select sampling modes but cannot recover that history.

## Coverage and required next inputs

Per-map reproduced hashes: **unknown for all three maps**. Uncaptured generated
hashes: **none generated**. Capture ownership cannot be recovered from the index
inventory or `pairs.txt`; do not treat all 571 accumulated hashes as each map's
denominator. The script does not load captures as generation templates or copy
them to outputs.

Exact historical reproduction would require the runtime submission/register-write
sequence, initial shadow state, and draw-time constants, plus map/pass provenance
for comparison. A static map walk cannot supply those inputs. An alternative
future task could define canonical program identities and keep constants as
runtime inputs, then derive canonical pass templates from tags. That would
require changing runtime hashing/lookup and regenerating the reference corpus,
which is outside this task's permitted edits. No runtime changes were made.

## Canonical runtime identities (2026-09-04)

The preceding audit describes the **legacy** capture identities and remains as
historical evidence. This change implements its proposed canonical identity;
it does not attempt to derive definitions from map tags.

Canonicalization copies all 240 bytes, then applies these changes, using the
layout decoded by `dx8_pixelshader_parse.decode_psdef` and consumed by the
unchanged `pixelshader_recomp_gen.generate`:

| Field | Canonical operation |
| --- | --- |
| Active count | `n = min(PSCombinerCount & 0xff, 8)`; retain the count word itself |
| Alpha inputs/outputs, RGB inputs/outputs | Zero word `i` for `n <= i < 8`, at bases `0x00`, `0x68`, `0x88`, `0xB4` |
| PSC0Mapping / PSC1Mapping | At `0xE4` / `0xE8`, clear nibble `i` for `n <= i < 8`; retain active nibbles |
| Constant colours | Zero `[0x28,0x68)` and `[0xAC,0xB4)` |
| Everything else | Preserve byte for byte |

In particular, final ABCD/EFG, final constant **selection** (`0xEC`), compare
mode, texture modes, dot mapping, input texture, and all global combiner flags
(including SAME/UNIQUE_C0/C1 and MUX_MSB) remain in the hash. Texture stages are
not combiner stages and are not cleared according to the combiner count.
For count zero all eight stage instructions/selections clear; for counts above
eight none clear. The count byte remains distinct; this does not validate or
reinterpret invalid definitions (the source parser clamps invalid counts).

Hash the canonical copy with the **same legacy FNV-1a byte walk**, still skipping
the colour ranges rather than feeding zero bytes for them. Initial value is
2166136261, multiplier 16777619, with 32-bit wraparound. The Python implementation
in `tools/psdef_hash.py` uses only the standard library. The C implementation is
`psdef_canonicalize` in `recomp/kernel/xd3d.c`, called immediately before
`psdef_hash` in `xd3d_ps_sync`. Live `ps_shadow`, constant uploads and logged
capture bytes remain intact, so subsequent push writes can reactivate retained
stage state. New logs carry a canonical hash alongside the live snapshot; the
pipeline accepts both legacy and canonical capture names.

### Equivalence proof and counts

- All **571/571** legacy `.bin` filenames match recomputed original hashes.
- **571 -> 191 canonical program identities**, with no FNV collisions.
- **Zero groups have differing generated sources; zero groups require splitting.**
  `tools/psdef_verify.py` parses and generates source from every member and from
  its canonical copy for every one of the 128 vertex-output masks and for the
  unrestricted interface. It compares complete source strings using one fixed
  name, so only the filename/hash comment is normalized. It does not strip
  arbitrary comments or alter generator semantics. All 129 configurations pass.
- The existing 191 diagnostic groups remain 191 when inactive mapping nibbles
  are also cleared. No field removed here affects the generated source in this
  corpus. Global and final fields remain conservatively distinct even if the
  current generator does not use every bit.
- The **518 logged pairs become 179 distinct table entries**, covering 152 of
  the 191 identities. Every original pair resolves to its canonical counterpart.
- The existing pipeline requires a vertex-output-mask suffix because GXM fragment
  inputs must match vertex outputs. There are **170 paired Cg variants**, plus
  **39 unpaired identities** emitted with the full `7F` interface: **209 Cg
  files total** (167 added, 42 already canonical) for 191 identities. These interface variants are not failed
  equivalence groups and cannot safely be conflated into one file per hash.
  Actual filenames remain `ps_<canonical>_<mask>.frag.cg` and corresponding GXP
  names, matching the existing generator and console compiler convention.
- All 499 previously tracked fragment sources remain in place and unchanged.
  Existing sources with corresponding canonical variants compare identically
  after replacing the hash in the generated-name comment.

Reproduce the corpus checks and regenerate from the accumulated inputs:

```sh
python3 tools/psdef_hash.py       # original-hash self-test, every group and size
python3 tools/psdef_verify.py     # source equivalence for all members/interfaces
python3 tools/ps_pipeline.py     # no log argument needed for existing captures
# Or ingest new logs before regeneration:
python3 tools/ps_pipeline.py /path/to/xita.log
```

The original-hash self-test is specifically for the legacy capture corpus; new
live snapshots named by canonical hashes need not match their original hash.
`shaders/psdefs/pairs.txt` supplies logged draw pairs; `shaders/halo_pairs.json`
is the unrelated vertex declaration/function pairing data. Original `.bin`
files and `pairs.txt` are retained. The pipeline derives aliases in memory,
deduplicates table entries and writes `tools/psdef_rename.txt` (571 mappings).
It fails on invalid capture identities, canonical hash collisions or generation
errors. Run the equivalence proof again when changing the generator or corpus.

### Reuse console-compiled shaders

```sh
tools/psdefs_rename_gxp.sh /path/to/compiled/shaders
```

This uses `psdef_rename.txt` next to the script (or an explicit second argument).
It hard-links each available old GXP to the canonical filename, falling back to
copying when hard links are unavailable. It preserves `_<mask>.frag.gxp` suffixes
and also supports plain `ps_<hash>.gxp` / `ps_<hash>.frag.gxp`. Missing sources are
skipped; existing destinations and all originals are retained. Repeated runs
are safe. Multiple members can supply the same destination because the generated
sources were proven identical for each interface. As with any reuse of compiled
assets, the GXP must have been compiled from the current generator's source and
cube mode. Unpaired identities without compiled sources still need compilation
when used with a known vertex pairing.

### Validation and runtime scope

The unchanged generator passed the full equivalence proof. A host C harness
extracted the actual C canonicalization/hash functions and compared all 571
captures plus six synthetic count/mapping boundary cases (0, 1, 7, 8, 9, 255)
against Python: all 577 agreed, with input buffers unchanged. The rename script
passed suffix preservation, directory names containing spaces, missing inputs
and repeat-run checks.

The only runtime implementation file changed is `recomp/kernel/xd3d.c`.
`shaders/xv_ps_table.h` is the regenerated runtime lookup data. `xv_d3d.c` is
unchanged: it already matches both vertex FNV and PS hash, so it naturally uses
the canonical hash passed by the kernel. No generator semantics changed.

Build check: regenerated the ignored game C sources using `tools/recomp.sh` and
the supplied XBE/Python environment. `make RECOMP=1 -j8` was interrupted while
compiling the large generated game translation units; a complete VPK build is
not claimed. The permitted focused check passed:

```sh
export VITASDK=$HOME/vitasdk PATH=$HOME/vitasdk/bin:$PATH
make RECOMP=1 build/recomp/kernel/xd3d.o
```

This compiled `xd3d.c` successfully with the Makefile's actual Vita ARM flags.
Game data and generated game C sources remain ignored and are not part of the
change.
