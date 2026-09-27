# Object-walk dead shift flags — private experiment

The next lead after perf266's periodic-logging change is the firing-related
object traversal 171AF0. Earlier aligned Pi diagnostics identify this branch,
but do not establish its current Vita self time. Its previous x87 register
candidate did not demonstrate a benefit and remains out of the hardware stack.

A new private candidate changes only the SHL results at 171B4E and 171B88 to
unsigned shifts with count masked by 31. The first is followed by MOV and TEST;
the second immediately by TEST. Neither path branches or calls before TEST
replaces the arithmetic flags. Count zero and wrapped counts retain the same
result. This is not permission to treat variable shifts as unconditionally
killing incoming flags in the general compiler's liveness analysis.

The emulated context is not byte-identical: dormant f_cf/f_of backing values
may differ after TEST clears their override bits. The existing differential
fixture normalizes only inactive backing fields, preserves CF for ADC/SBB,
and compares all other context, full arena and callee boundaries. Thus this
proves less than strict complete-context equivalence; callers and observers
must not consume dormant backing values directly before broader integration.

Validation completed:

- Host O1 ASan/UBSan: 10,000 cases, 46,187 candidate callee observations.
- Pi Cortex-A9 Thumb O2, thread page tables/render views: same cases passed
  on core 0. All eight x87 TOP inputs and recursion depth two exercised.
- Same-flags ARM object comparison: 4,064 bytes reference text versus 4,004
  candidate (-60 bytes). This is code size, not an instruction/time result.

Private files: ../object-walk-shifts/ (candidate, source hashes, host/ARM build
commands, test logs, cost objects and size.json). No generated code is committed.
No recompiler default, runtime helper, or Vita build was changed. Perf266 remains
installed. Before qualification: audit raw flag-field consumers and run the
aligned Pi gameplay harness to determine whether any measurable work is saved.
The small size reduction does not justify expecting the full firing deficit
to disappear. The sustained hardware 20 FPS objective remains unmet.
