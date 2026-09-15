# Halo 2: dispatch through the original field-44h mapper

Native151 and native152 reach `216A50` from `2170AF`. The function reads
the original record's dword at offset `44h`, decrements ECX, and jumps through
`216A88[ECX]`. Both traces stop at `216A54`, ECX 6, return `2170B4`. The
original index selects `216A7F`, which returns 5. Its caller stores that
original result in a copied descriptor.

This second sparse table contains nine entries, with null holes at indices
4 and 5. The original code group is bounded by its table at `216A88`, and
the table is followed by `CC` padding at `216AAC`. The neighboring `216AB0`
mapping uses a separate dense table; it is not added to this fix.

Both the 56-byte code group and 36-byte table are fingerprinted. Their hashes
are `e59cb4e3aba68854c97772c9fbd695217fb19d4e4caa305730d804b5d7c7b835`
and `3a12572e353c32577d20ce9d33cf70adfc65dd9b8f3227b14b40c0209baf7005`.
The scoped sparse-JMP hook now accepts ECX indexing only at this exact site.
It reads the actual table word and tail-calls the original target, preserving
the current return address. Null and unknown targets still stop. Original
record reads, the decrement, target implementations and descriptor copies
are unchanged; there is no substitute mapper or forced result.

All 40 sparse-jump/callback/profile/loop tests pass. The new ECX fixture runs
generated C and compares complete context and return-address memory, including
null holes, negative indexing, changed live table words and the unchanged
default compiler path. Regeneration adds seven original target functions,
11,944 generated functions total; these counts do not establish runtime or
menu coverage. Private audit is `audio-host/native151-getter-audit.txt` and
generated output is `widget-field`.

Native153 combines this dispatch with the separately validated staging-mask
initialization. Its replay must reach a new original boundary before further
progress is claimed. Owned game data, shaders, generated code, traces and
diagnostic packages remain private; packages must not be distributed.

Native153 passes the field mapper. The original descriptor walk at `108A40`
then reaches untranslated callback `DC4C0`, return `108A6F`, through a child's
field `28h`. This is the next bounded discovery task. The Microsoft Game
Studios intro is visibly rendered; the first movie input/output are equal.
The final frame remains black and the original main menu is not visible.

All 171 completed dependency targets are verified. Native153 ELF SHA-256 is
`7f7b6c8137475c561d75cd670b57a684b58c9172087484b243ff0557d9d57d8f`,
EBOOT `3657c8d527c9d99b7ec440fbbc331b8285cb8d26efb065f0593de0969bbf0ced`,
trace `0006260c39b563b0f86d9d29fdc7af75129e7197a0bdb532ed92f80bb83b7604`.
The exact package, direct capture, normal Start receipt and terminal state are
in private `native-153-artifacts` and `native-153-view` directories. The owned
emulator has exited, with no native build still running at this checkpoint.
