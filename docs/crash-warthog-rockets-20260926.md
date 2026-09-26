# Blood Gulch Warthog / rocket freeze — September 26

User reproduction: drive the Warthog, then shoot rockets at it to test physics.
The installed build was **0.2.0-perf.253**, before any tester-updater or dashboard
changes were deployed. The latest log, preceding log, freeze marker, core dump
and six screenshots were archived privately before reopening the application.

The watchdog reported no presented frame for 40 seconds at frame counter 12346,
process time 859.3 seconds, then intentionally trapped to produce the core dump.
The final screenshots show the sky and a flat pale field with world geometry
absent. They are not evidence of an ordinary steady-state FPS result.

Symbolizing against the matching perf253 ELF places the running guest thread in
`f_00091EE0`, with `f_000939C0` and `f_00180ADA` in the stack scan. The render pump
is waiting. This establishes a guest-side investigation target; a stack scan
alone does not prove the cause or establish that the GPU driver crashed.

The stopped native PC is `0x81b76d42` (ELF address `0x81b6ad42`), in the
cluster light-reference chain advance inside `91EE0`. Registers identify a
reference at `0x800ad74c` and a next handle of `0xde850055`. Relative to the
load-time reference table at `0x800ad338` with 12-byte entries, these correspond
to indices `0x57` and `0x55`. This is not a demonstrated self-loop; a longer
cycle or inconsistent list remains a hypothesis. The core dump does not contain
the guest arena pages needed to reconstruct the complete chain.

The log reports 16 abandoned scenes, including 15 timeouts, before the final
freeze. Snapshot merging after an abandoned scene is an investigation target,
not an established cause. The reproduction involves vehicle physics and rocket
effects, but the stopped thread does not establish a physics-engine failure.

Next: add bounded freeze-time capture of the light-reference tables and heads,
then check for invalid/cyclic chains and their writers during vehicle explosions.
Keep diagnostic capture off the normal frame path. Do not hide a corrupt chain
by silently skipping objects or ship a speculative physics fix with the
dashboard work.

## perf255 diagnostic candidate

Developer builds now capture `ux0:data/xita/freeze-lights.bin` immediately before
an enabled `XV_FREEZE_ABORT` watchdog traps. There is no added per-frame work,
no list repair, and no change to scene abandonment or snapshot merging.
Tester builds compile out the capture and its storage. Incremental distribution
changes also rebuild the kernel object containing the capture.

The fixed v1 little-endian record is 53,416 bytes: a 16-byte header followed by
two 26,700-byte samples. Each sample contains a validity mask, four globals
starting at `0x2fc670`, the 56-byte reference header, 512 candidate cluster heads,
and room for 2,048 12-byte references. Capacity and stride are checked before
copying records. Reads use the live page table, reject trash/shadow mappings,
and check every page; partial copies do not receive a valid flag. Storage is
static so the 16 KiB watchdog stack and potentially blocked allocator are avoided.
The output writer handles short writes; a failed/partial write does not prevent
the subsequent core dump trap.

Inspect a retrieved file with `python3 tools/inspect_freeze_lights.py FILE`.
Add `--head INDEX` only for a known active cluster. The fixed head capture can
include unused slots. Equal samples are repeated observations, not proof of
an atomic snapshot or synchronization; cycles in a capture still require
correlation with the stopped thread and mutation history. Handle salt validity
is not checked by this traversal test.

Validation: bounded reads, cross-page records, sentinel termination, salted
indices, cycles, invalid indices, invalid capacity/stride and unmapped pages
passed host ASan/UBSan and Cortex-A9-targeted ARM tests on the Raspberry Pi.
The decoder rejected a partial capture and identified a synthetic two-node
cycle. The full developer Vita build linked successfully with the capture
symbol. Hardware reproduction and the cause of the freeze remain unverified.
