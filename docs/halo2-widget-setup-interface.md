# Halo 2 original incoming-widget setup

Native 176 stops at `22F1F7` from `234EBC`, return `234EEC`, object
`82537E90`, vtable `4587D0`, ESP `005E5F80`. This is a setup callback,
not a constant query: the parent first invokes virtual `44h`, then `48h`
with its state pointer, followed by `4Ch` and conditionally `64h`.
Only after that sequence does it transfer the incoming object from `0Ch`
to `8h` and continue the original update walks.

Constructor `22F15D` calls base constructor `22F5CA`, then installs the
28-method derived table `4587D0`. Its boundary is the separately bound
base table `458840`. The observed setup builds its original local state,
initializes child/focus data through `22F8DF`, links its event node, calls
the original state update and records the guest clock. Discovery translates
the original table; it does not pre-activate the widget or create UI state.

Both constructors also install the one-method event interface `45BDB0`,
followed by a zero word. Its original `2B27F9` wrapper loads the saved
receiver at `10h` and tail-jumps through the saved callback at `14h`.
The constructor callbacks `22F3B5` / `230427` are already translated.
Their complete fingerprints are included alongside the wrapper and
constructors; no callback replacement or return-value override is added.
Original list membership, event arguments and lifetime handling remain intact.

Seven full fingerprints cover the parent walk, constructors, observed setup,
event wrapper and stored callbacks, with the existing whole-image gate.
Only the derived table and the single event method become roots; the base
and following event tables are not scanned. Every target must be executable
title `.text`, and the event interface's zero boundary is required.

All 66 focused callback, profile, LOOP and sparse-jump tests pass, including
every guard, invalid/non-title targets, missing section metadata, the zero
boundary and untouched neighboring slots. This changes only Halo 2 discovery;
no shared runtime, sound or graphics behavior changes are included.

Private proof: `animated-widget/widget-setup-guards.json`,
`widget-setup-full-bodies.txt`, `widget-setup-references.json`.
Regeneration adds ten function entries, sixteen blocks and a net 171 emitted
instructions. Inferred switch targets change from 4,654 to 4,645; a separate
comparison of emitted guest addresses finds no removed addresses or changed
instruction decodings, and 204 new unique addresses. Net emitted counts include
overlapping translations and are not runtime coverage. Unsupported instructions
remain 3,792. The switch difference is a removed duplicate expansion:
`14800C` now tail-calls existing `236299`, whose nine original switch cases
remain intact. Private comparisons: `widget-setup-coverage-diff.json` and
`widget-setup-switch-diff.json`.

Native 177 executes into original child setup, then stops at `2540D3`
from `22E65E`, return `22E6C9`, object `825384A0`, vtable `45A628`,
ESP `005E5ED4`. That object address is the prior incoming object's `610h`
member, initialized by the fingerprinted constructor. This establishes
progress into setup, not completion of activation or execution of every method.

The original Microsoft Game Studios intro is visibly confirmed; normal Start
follows the complete 59,670,016-byte map copy. The transition clip contains
56.4 seconds. No main menu appears. The final frame remains black (frame 135),
and decoded channel/push snapshots are unchanged.

All 171 native dependency targets pass verification. Frozen artifacts and
hashes are in `native-177-artifacts` and `native-milestone-177.json`:

- ELF: `a86d93ba0a26e48c9794ea7013919d66177af3052c5018de3511a1ec0b53851a`
- EBOOT: `6951aa5c436ef9ec3363a37355537279d2ac096701bf2eae4a451bc4c41c4de2`
- Guest trace: `bd0b84b587f5175ce472d6bb00a9ce258211ea3f22eb7e86d4eb020234988350`

From the private directory, replay with
`python3 preserve_fresh_cache.py native177-replay`,
`python3 capture_run.py 177-replay native-177-artifacts`, then
`python3 drive_startup.py 177-replay native-177-artifacts`.

The diagnostic package embeds owned game content and must not be distributed.
