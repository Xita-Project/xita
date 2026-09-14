# Halo 2: sparse widget kind dispatch

Native150 reaches `216B00` from `1489BD` with kind 4. The original decrement
produces index 3, then `216B01` jumps through table `216B30`. Discovery had
retained only the first two entries, stopping at the first null hole, so the
generated switch trapped on this valid original index.

The complete 48-byte code group and 36-byte table are fingerprinted. The
table contains nine entries with null holes at indices 2 and 5, followed by
`CC` padding. Its seven distinct targets return the original mapping values.
The native index selects `216B0B`, which returns 1. No replacement mapping
or success value is supplied by the adapter.

The existing Halo 2 sparse-jump lowering now also accepts this exact JMP
instruction shape. It reads the actual table word and uses ordinary indirect
tail dispatch, preserving the caller's return address and original target
code. Null and unknown targets retain the checked dispatcher stop. No new
index clamp or general compiler discovery change is introduced. Both code
and table fingerprints are mandatory in host-channel preparation.

All 38 combined sparse-jump/callback/profile/loop tests pass. The synthetic
tail-dispatch fixtures execute both scoped sites and the unchanged default
path, checking complete context, return-address preservation, null holes,
negative index addressing and a changed live table word. No owned executable
bytes appear in these tests. Regeneration produces seven additional target
functions, 11,937 total; these are automatic translation counts.

Private audit is `audio-host/native150-sparse-dispatch.json` and generated
output is `widget-kind`. Native151 is the corresponding exact-build replay.
Owned game data, generated code, shaders and packages remain outside Git;
the diagnostic package embeds owned content and must not be distributed.

Native151 passes the observed kind mapping and stops at the next original
sparse jump, `216A54` in `216A50`, return `2170B4`, ECX 6 after its decrement.
This adjacent getter has a separate table and requires its own validation.
The movie output remains black on this unchanged-renderer control. The
original main menu is still absent.

All 171 completed dependency targets are verified. Native151 ELF SHA-256 is
`3e70de3ed7bc47e0945f248e84cbec8b2958157011ca92a701c9cb44dd69cbf3`,
EBOOT `cff6ad3fc42c26430979d3add9e5d3fc4098642c608e4bec1468c85c558ec0b7`,
trace `fd1664f1a7616deb2922d02cc4ffe5ae5552a7febb04b6030c6302925c75e323`.
The exact package, normal Start receipt, shader evidence and terminal snapshots
are preserved under `native-151-artifacts` and `native-151-view`. This replay
does not include the separately tested staging-mask change.
