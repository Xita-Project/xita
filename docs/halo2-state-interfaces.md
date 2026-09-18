# Halo 2 constructor-bound state interfaces

Native58 reaches the original state dispatcher `6DEC0`. Its indirect call at
`6DF3B` selects object `52738C` and stops at undiscovered method `6E130`, return
`6DF3D`. The original method is retained and executed by the next build; no
successful replacement or synthetic state transition is installed.

XAPI initializer `376A20` places `527330` in EAX and tail-jumps to constructor
`58E70`. That constructor assigns eleven four-method vtables to subobjects.
The first observed object is exactly `527330 + 5Ch = 52738C`. The assignments
and both function fingerprints establish the bounded group:

| Subobject offset | Vtable |
| --- | --- |
| `5C` | `450A00` |
| `6C` | `4509D0` |
| `8C` | `450A10` |
| `A0` | `450990` |
| `B8` | `450A30` |
| `C8` | `450A20` |
| `1D0` | `4509E0` |
| `C68` | `4509B0` |
| `C88` | `4509A0` |
| `CA8` | `4509F0` |
| `CB8` | `4509C0` |

The 44 slots occupy `450990..450A40`, surrounded by non-code string data.
Every included target is executable `.text`. Preparation validates all slots,
the 10-byte initializer fingerprint
`ea990a0b2671f169030c7ed2bac29c98c5c5e42fd4bafcb4d5fcf86da5ff161c`, and
the 105-byte constructor fingerprint
`9195f198f6de23c02ee21bf68539b3898e5bad37deff50056be9c0b6b35b1e29`.
The identity profile and Halo CE discovery remain unchanged.

Synthetic bounds tests cover every slot, rejection of an invalid final target
and constructor mismatch. All 13 callback-root tests pass. Discovery roots do
not establish interface compatibility; only subsequent native execution can
validate an exercised path. Owned disassembly, generated code and game-embedded
diagnostic packages remain private and outside Git.

Native59 executes the previously missing callback and advances to another
original interface call: `73062` invokes slot `+1C` of vtable `450B68` on
object `47708C`. Its undiscovered target is `73360`, returning to `73065`.
This is the next discovery boundary, not a graphics command. The decoded
channel remains at GET/PUT `03B43280` with no new draw submission.
The actual `native-59-view/window-2.5.png` capture is black; no menu or
main-menu map load is observed. The first scanout retains SHA-256
`a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1`.

Private native59 artifacts:

| Artifact | SHA-256 |
| --- | --- |
| ELF | `d9917863da7446c90aba915a3fd1dcf61bcfa1764d82f356f8afb5f933ca0743` |
| EBOOT | `f5787c0f190228344cb5aabcaf7e59cecf421f5a09be9f61ca65dbb6d8b2587c` |
| VPK | `ab024265573891c4f88e7fea8ab7b27a7c3f8552c8ad23edbe34350dfd2f4933` |
| Boot trace | `aae05216a7ffb4a530328d3cf300aabbec9a0aebdc5418334e1bf7c5401585d6` |
| Decoded channel | `bd6eaf29545dd91c4c4c280abab21c49e597e178e48c017326176586a9a1e962` |

Generated source is frozen privately in `state-interfaces/generated`.
Reproduce the exact diagnostic build from the source directory with
`python3 ../private/run_lab.py replay59 ../private/native-59-artifacts/halo2-boot.vpk`.
The package embeds owned game image/code and must not be uploaded as a
distributable release. This run uses the explicitly unavailable-audio
probe and the limited masked native FP bridge described in their respective
notes; it does not establish behavior without those options.
