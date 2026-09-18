# Halo 2 animation keyframe callbacks

Native155 advances through packed decoder `28C470` and then reaches `28CDB0`
through global `504484`, return `27AB57`. That callback appears in both format4
and format6 records of the same table. Their two triplets are identical:
`28CDB0`, `28CF40`, `28D090`, then `28C9E0`, `28CB70`, `28CCC0`.
The terminal trace alone does not distinguish which of those two formats was
selected, and no such distinction is claimed.

The previously fingerprinted original binders select the first callback at
`47FB24 + format * 40`, then choose the triplet at offset0 or12. Preparation
now also roots the six fields starting at `47FBC4` and `47FC14`. It excludes
the other field at offset24 and the neighboring records' metadata. Both
24-byte callback spans have SHA-256
`5a54a196ac148cc555c7bd7bddf8eff1f84ed6759bea432f9c3fa4dc335befae`.
Every target must be nonnull executable title code; duplicate entries resolve
to the same original function. Unobserved formats remain outside this rule.

The original routines retain their binary keyframe search, exact-key versus
interpolated branches, frame/fraction reads, packed signed-component
conversion and reciprocal-square-root normalization. No animation result,
callback, branch or timing value is substituted. This change adds only
callback roots, their synthetic bounds tests and this report; the validated
instruction implementation from `ff7313f` is unchanged.

All44 focused Python checks pass, including the generated SIMD tests. The
callback fixture tests every selected field in both triplets for all three
covered rows, rejects null/noncode/wrong-section values, verifies the caller
fingerprints and ignores adjacent fields. Private audit and native156 output
are under `packed-decoder/native155-next-rows.json` and `packed-keyframes`.
The diagnostic package embeds owned game content and must not be distributed.
Native replay results follow when complete; the original main menu is not yet
visibly verified.

Native156 passes these callbacks and reaches the next original descriptor
child callback, `112070`, from `109050`, return `1090A7`. That caller reads the
child's field`6Ch` and pushes the original object handle plus three arguments;
it remains a strict missing-function stop pending its own discovery audit.
Regeneration adds six original functions (11,981 total),42 blocks and663
instructions, with the unsupported count unchanged at3,825. These are
emission metrics, not demonstrated compatibility.

The first sampled window was black. A later actual window recording visibly
shows the Microsoft Game Studios intro in
`native-156-view/movie-window-middle.png`, SHA-256
`1076cc31adeb23d2f4e3b7dc4779ab63bc9b1f445f4136d5b2b8c57a97311dc0`.
The recording ends when the controlled strict stop closes the window; it
must not be presented as a complete eight-second capture. Final frame135 is
black, and the main menu remains absent. No presentation changes were made.

All171 completed dependency targets are verified. ELF SHA-256 is
`68f64738587f28cf484c1b3f0ff9d97135ccc7092287c909fee0c3aeafefef4e`,
EBOOT `6f21a7c61769b8eede9eb42f863fde654307539f7391bc98d7429ebdec1b8b0c`,
trace `9f4d5e6b59736a039ef92a3241ec5d52470ee3606423185b135aed85c8d0cfe1`.
Exact artifacts and normal Start receipt are preserved privately under
`native-156-artifacts`, `native-milestone-156.json` and `native-156-view`.
The owned emulator and native build are stopped at this checkpoint.
