# Halo 2: text widget callback chain

Native147 stops at `22F57F`, invoked through table `458940` slot `3Ch`.
The original getter returns the embedded object at offset `74h`; caller
`253BF6` then invokes that object's slot 4. Factory `22F9BF` selects between
two original constructors using bit `10h` of its argument. Both assign the
outer table `458940`; their member constructors assign `4588B0` or `458930`.

Preparation now fingerprints all four complete constructors and roots only
the executable prefixes: 17 slots at `458940`, three at `4588B0`, and three
at `458930`. Each following zero word is checked. These are selected prefixes,
not claims about the complete class layout. Adjacent tables remain excluded.
The change applies only to the existing Halo 2 host-channel preparation;
every callback executes its original translated implementation.

All 22 synthetic callback tests pass. Regeneration adds 14 original functions,
from 11,854 to 11,868; this is automatic translation coverage. Native148 passes
the getter/member chain and reaches the next strict indirect target `236AE6`
at `2373F4`, return `2373F6`. The caller explicitly bounds its table index to
`0..6Fh` before reading `470828[index]`. That table needs separate discovery
validation. All four immediate effect writes still complete on this path.

Native148's movie input is nonblack but rendered output is black, despite
fresh `.spv.txt` shader generation with preload disabled. The same renderer
was visibly working in native147. This discrepancy is preserved and requires
a controlled replay; it is not attributed to a specific cause yet. The direct
capture `native-148-view/window-before-start.png` is black. There is no main
menu. Native147 remains the latest verified original intro capture.

The completed `text-widget/build` has 171 verified dependency targets.
Private audits, generated code, packages and captures remain outside Git.
Packages embed owned game content and must not be distributed.

| Native148 artifact | SHA-256 |
|---|---|
| ELF | `622ee2556b96e9ef085cccdc9e6e8b716460f97f0042ffb59c53d7985e991d95` |
| EBOOT | `f06861fe7afd5eb59d9e59fa119369514c1770e9a4f391e91b19ed9a90cbf32d` |
| VPK | `69403394dd6f0cafeadc214e666ac19498b65bd2da3d8017146324e21b674daa` |
| Guest trace | `586c07be04b3ad1c791ff32d62e4e6614574936133d5d9f903e62cd0db4b4fc3` |
| Channel snapshot | `1c7ddb1861cdeb9752608b320b8e279d7b60a6e05a48990b8afba3b78eaac4ff` |

Replay uses the private fresh-cache procedure with the archived package in
`native-148-artifacts` and a unique attempt label. Native149 is an exact-build
control with the same configuration and fresh game cache; its outcome must
be assessed independently. The main-menu objective remains active.
