# Halo 2: original widget property callbacks

Native148 and its exact-build control native149 stop at `236AE6`, called at
`2373F4`, return `2373F6`. The original caller `2373BE` accepts only indices
`0..6Fh`, reads `470828[index]`, skips null entries, and calls the selected
callback with three output pointers. The observed callback returns kind 5,
offset `4Ch`, and an original scale value. The caller uses these original
outputs to update the widget. None of that behavior is replaced.

The 56-byte dispatch fingerprint is
`863a8fd2961e9fd5ceab6711205222d5cf403957a5512768286bceb656ca4c61`.
Preparation includes the 112 entries at `470828..4709E8` (exclusive end),
retaining ten null entries and checking every nonnull target is title code.
The 102 nonnull entries name 62 unique functions. The owned table fingerprint
is `677f33567a0d3a12d5479e2e04bab4d5aae801fe6c13ef05145826358289c903`;
the complete owned-XBE revision guard remains mandatory. This is a bounded
indirect-call table derived from an executed caller, not general data scanning.

All 23 callback tests pass, including bounds, holes, first/middle/last invalid
targets, non-code targets and caller revision changes. Regeneration adds
exactly the 62 original callback bodies, with no removed functions or new
runtime behavior. The total 11,930 generated functions describes automatic
translation coverage, not runtime validation or a displayed main menu.

Native149 replays the exact native148 ELF/EBOOT/VPK with unchanged SPIR-V
configuration and a fresh private game cache. It visibly renders the original
Microsoft Game Studios logo; its first nonblack movie input/output are again
byte-identical. Its capture `native-149-view/window-intro.png` has SHA-256
`d9d44d6b6999887f9106d073b5a0efeb60507c9a5e1b88b19dba23f65df66694`.
Native148 instead rendered that same movie input black. Both outcomes and
the exact-build control are preserved. The intermittent rendering discrepancy
is unresolved; selecting SPIR-V is not a complete reliability fix.

Native149 returns to the same strict callback stop after normal Start, and
its final frame is black. Neither run shows the original main menu. Its
private shader evidence, trace and package are in `native-149-artifacts`;
property-table audit and discovery differences are in `text-widget` and
`widget-properties`. Owned assets, generated code and packages remain outside
Git; these packages must not be distributed.

Native150 executes past the property callbacks, then stops at `216B01` in
`216B00`, return `1489C2`, with EAX 3 after the original decrement. This is an
original sparse indirect-jump table whose discovered prefix ended at a null
hole. The remaining entries require explicit validation and tail dispatch;
the stop is retained. The movie output is black in this run, even though both
generated SPIR-V files match native149 byte-for-byte. No main menu is visible.

The completed native150 build verifies all 171 dependency targets. Its ELF is
`8ae663c6bd6d652a5785898e410cf8c0791d62df23b4a35f8ec1f0cfe8d3e236`,
EBOOT `63b3ed6c8e0a312255e27d80b340604fe0d06500a03ba485f6fb9e34d3e3becd`,
trace `8284218b295ed3d25d26c8298a6a1d87b37b7cacdf9897144e30c4290cd90a7c`.
The package, exact shader evidence, channel snapshot and normal Start receipt
are archived privately under `native-150-artifacts` and `native-150-view`.
All 34 combined callback/profile/loop regression tests pass. The owned
emulator has exited; no native build is left running at this checkpoint.
