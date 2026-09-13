# Halo 2 registered interface methods

Native51 reaches original interface object `462E00` and stops at method
`3331CC` through vtable `417378` slot `28h`. The call is `332780` in `33275E`,
return `332783`. The method allocates and initializes its actual per-object
storage; no successful substitute or synthetic object is introduced.

The original XAPI initializer `3769F0` copies four static object pointers into
`52731C..527328`. Its 45-byte fingerprint is
`46e548c6c8f362dc1ba57b6f7581a1b2c0bffb4b2cb9c2e812dcc7b9544b1611`.
Preparation checks that fingerprint and each exact pointer/vtable binding:

| Pointer slot | Object | Vtable | Method words |
| --- | --- | --- | --- |
| `417370` | `462E00` | `417378` | 27 |
| `4173E4` | `462E10` | `4173E8` | 27 |
| `41745C` | `462E28` | `417460` | 27 |
| `4174D0` | `462F30` | `4174D8` | 27 |

Every included method pointer belongs to executable `.text`. The first three
tables end before non-code data at `4173E4`, `417454` and `4174CC`; the fourth
ends at `417544`, a separately assigned XPP table (original assignment
`411319`) whose code lies in another section. That XPP table and neighboring
metadata are excluded. These are four specific registered objects, not a data
section scan or a claim that every interface method now works correctly.

Synthetic tests cover all 108 table slots, exact bindings, the final slot's
validation and initializer fingerprint rejection. All 12 callback-root tests
pass. Other startup roots and the strict unavailable-function stop remain in
place; the game executes every original method body.

Native52 executes past these methods, creates the real 24 KiB kernel stack,
initializes a timer and DPC, then stops at the generated sparse-table jump
`18EBDD`, with EAX=5, ESP=`005E4C7C`, return=`18EFAF`. This was a discovery
error, not an original guest assertion; the following
[sparse-jump checkpoint](halo2-sparse-state-jump.md) documents its correction.
Native52 remains black, with no geometry or menu. Its decoded channel JSON
still matches native48 (`f461fd24...`, GET=PUT=`03B54280`).

Private generation is `registered-interfaces/generated`; exact packages and
captures are in `native-52-artifacts` and `native-52-view`.

| Native 52 artifact | SHA-256 |
| --- | --- |
| ELF | `19f72735d73d951107c7ad31f8dfebcbc1ec827f34e1f6474ed2e1e8c8adf37a` |
| EBOOT | `de613a41a0f9270e77d253440045620493574a55135582ba614eef3be9b170da` |
| VPK | `94abcc9fba79d2928a2214ddb7c84e4505723c0630ac7f5ab8c63c37a88a9a68` |
| Boot trace | `83f8db4c4e200e0b227a69c0130e1bea83d66c194cca80cd93ef6c0083047910` |
