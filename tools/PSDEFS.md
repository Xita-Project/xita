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
