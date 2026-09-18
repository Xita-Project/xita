# Halo 2 original movie converter callbacks

Native69 passes the previously missing movie converter callback and reaches a
new original graphics submission. The image-backed descriptor at `57A080`
contains scalar/function pairs, mutable counters and a second function array.
The revision-checked wrapper `3EDB70` passes this descriptor to `3E9C70`; the
fingerprinted setup routine `3E97E0` selects its function fields.

The diagnostic preparation step adds only the six function fields at offsets
`08,10,18,20,28,30` and sixteen fields at `74..B0`. Seven scalar fields must match,
every callback must point into an executable Bink code section, and both caller
fingerprints must match. Selection counters and following data are not roots.
The exact owned image has 22 distinct validated targets. Synthetic tests cover
all fields, shape changes, executable/section checks, null targets, fingerprints
and exclusion of mutable/following data. All 30 focused Python regressions pass.
No game instruction, callback selection or return value is replaced.

Actual Vita3K native69 advances beyond `3EDC20` and stops at class `97` method
`0260`, value `18111912` (combiner alpha input state). GET is `03B44038`, PUT is
`03B44520`; the rejected submission returns through guest `3FAC58`. The decoded
channel snapshot contains the original uploaded 12-slot vertex program and
constant state. It does not prove valid texture bindings or executed geometry.
The actual capture and first presented buffer remain black: no movie frame or
main menu has been displayed. The next task is strict combiner-state handling
and investigation of the following original draw submission.

| Private native69 artifact | SHA-256 |
| --- | --- |
| ELF | `d7e34bf969983df2d50d1f6a746432072f12fb160ec875305187eac21a142513` |
| EBOOT | `749a80281fb4ca781e42a0c18cb3253daeaec261ab45b15d33d650dd1dfc9fe8` |
| VPK | `b82b6df6c5a7ea26561f503e6b4c3c0583fb93cab9763a6f821fb535fe3162f6` |
| Boot trace | `0c1ae3027ff9d05cf477ef26f08cd21a80ae7a4888670f8b8aa5d7a0c081dd98` |
| Channel snapshot | `c6b469fcb6b8c4eff693dd317633dc415607ecf6ab98bdbda3391eaf07eb7ac3` |
| Last presented buffer | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Replay: `python3 ../private/run_lab.py replay69 ../private/native-69-artifacts/halo2-boot.vpk`.
The frozen generated source is `private/bink-pixel-callbacks/generated`.
The owned game image/code embedded in the VPK must not be distributed or uploaded
as a release. All game data, generated source, packages and captures remain
outside Git. The unavailable-audio diagnostic remains explicit.
