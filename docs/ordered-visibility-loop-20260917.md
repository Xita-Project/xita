# Ordered visibility loop in the cumulative build

Source `c9639b1` adds an optional native integer loop inside Halo CE's
`532E0` visibility traversal. It retains the preceding twenty-one selected
optimization paths, graphics settings, worker policy and scene diagnostics.
This is a compatibility-qualified addition, not a demonstrated FPS gain.

## Change and boundaries

The loop keeps frequently used guest integer registers and lazy flags in
local variables. It preserves ordered guest reads/writes, recursive traversal,
clipping and projection children, path bits, callbacks and budget checks.
State is published before an existing child call or preemption and reloaded
afterward. It does not run the traversal concurrently or skip geometry.

`XV_NATIVE_VISIBILITY_PORTAL_LOOP` defaults to `0`. Explicit `1` requires the
qualified Halo CE profile and one marked generated body. The generator pins
the owned image, symbols, complete original body and shared runtime header;
generated game material remains outside the repository. Only `code_009.o`
changes in the cumulative package; 93 other objects remain identical.

## Qualification

- Thirty-five completed ARM scenarios match 1,950 observed state snapshots
  and 10,295 ordered guest writes per lane. Four deliberately incorrect
  variants are caught. Actual retained clipping/projection descendants run.
- Seven real Make transitions verify default/OFF identity, repeatable ON
  output, rebuilding when the option changes and no rebuild when it does not.
  OFF restores the complete previous object byte for byte.
- Independent inspection matches the production function to the qualified
  instructions and relocations. All 257 unrelated functions are preserved.
- Twenty-seven generator rejection cases preserve their output destinations;
  ten Make rejection cases and three admission controls pass.

The function's static stack frame increases from 80 to 96 bytes. A 32-level
recursive fixture uses 3,144 instead of 2,640 bytes; this does not prove a
universal recursion bound or every hardware caller's available stack. One
clipping-capacity case remains unqualified because the unchanged reference
exceeded the fixture's instruction ceiling. Scheduler services are modeled
explicitly; host qualification does not establish crash-free hardware play.

Modeled whole-function instruction counts fall about 6% in two traversal
cases, 0.8% in another, and rise by five instructions in a leaf case. These
are not hardware cycles and cannot be converted into an FPS prediction or
applied to the entire inclusive visibility interval.

## Package identity

- Runtime: `e37d73662b7a0d0dedb3597d77eae9e7c797f3598567709f8e6f4aa3e77cf726`
- VPK: `5220f7b9901182eadf3a549c8de676144e41ae76a63f996beaec75c07e91e5aa`
- Executable size: 31,974,238 bytes.
- The 1,588-member package retains the update contract; only `game-a.self`
  and `boot-game.txt` differ from the preceding package.

Private build, independent review and deployment receipts are retained under
`visibility-portal-startup`. Ordinary gameplay after a confirmed new boot is
the next hardware check. No built-in benchmark result is used to claim a gain.

## Other candidates

The material-builder prototypes remain separate. Full-context and GPR-only
variants increase modeled work; a narrower dataflow variant has small mixed
instruction/memory changes and incomplete real publication-path coverage.
They are preserved for further work, not labeled hardware FPS failures.

Compatible changes with inconclusive individual FPS results remain combined.
The next larger investigation is a safe boundary for owned visibility math
jobs, followed by ordered publication. Stable 20 FPS and an additional five
FPS remain unverified.
