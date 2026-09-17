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

The remote updater confirmed this runtime booted in slot 1 at
2026-09-17 19:49:36 UTC. The preceding `01a03548` runtime remains in slot 0
for rollback. The dashboard screenshot and subsequent Halo menus were
captured after restart. Private build, independent review and deployment
receipts are retained under `visibility-portal-startup`. No built-in benchmark
result is used to claim a gain.

## Ordinary hardware check

New002/New003 Blood Gulch loaded through the normal menus. The route walked
away from blue base, changed views, charged/released the plasma pistol, sent
two further fire inputs, walked again and paused. Captured charge use changed
the weapon indicator from 100 to 89. Screenshots, inputs and a 1,518,589-byte
log are retained; capture time is 2026-09-17 19:57:41 UTC, remote frame 9,032.
No searched crash marker or logger error was found. This short check does
not clear the previously intermittent crash or qualify every map/campaign.

Startup confirms native 960×544, triple buffering and effective CPU 444,
bus/GPU/crossbar 222/222/166 MHz. Palette/hierarchy overrides remain enabled.
No graphics setting was saved and benchmark mode remained zero. The initial
screenshot request during launch timed out before a completed display frame;
the game subsequently reached the menu and continued through the route.

The unchanged prior analysis script selects twenty windows using complete
owner/main/detail accounting, pass gates and qualified adjacent windows.
Independent review preserves those outputs and additionally excludes profile
window 7,560, whose following window straddles the pause. The nineteen-window
conservative summary has group counts 5/4/6/4, median FPS
10.7/13.5/14.85/13.95 and median frame times 93.2/74.2/67.35/71.55 ms.
The exact controller-to-profile boundary remains uncertain: log profile IDs
and the remote total-frame counter are separate counters, and complete
accounting plus unchanged endpoint state cannot prove every interior frame
was active. Known loading/mixed/paused rows are excluded.

These are ordinary-play observations of one executable. Spawn, camera and
world state differ from the preceding trial, so no before/after improvement
or regression follows. No further gameplay was run to produce the summary.

## Other candidates

The material-builder prototypes remain separate. Full-context and GPR-only
variants increase modeled work; a narrower dataflow variant has small mixed
instruction/memory changes and incomplete real publication-path coverage.
They are preserved for further work, not labeled hardware FPS failures.

Compatible changes with inconclusive individual FPS results remain combined.
An initial private owned-packet visibility experiment preserved the tested
state by replaying scratch writes, but its transport nearly doubled modeled
whole-tree work before threading. That packet shape will not be connected
to workers; the result does not reject all visibility parallelism.

The older optional B7F10 clipping region is compiled but still defaults OFF,
and is not one of these twenty-two selected paths. Its earlier mixed physical
comparison did not establish a material regression. A separate audit is
checking a safe startup selector at the joined recording-owner boundary so
it can receive a restarted cumulative trial. Stable 20 FPS and an additional
five FPS remain unverified.
