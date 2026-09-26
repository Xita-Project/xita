# Effect-processing physical x87 slots candidate

Status: experimental and not deployed. Goal remains sustained 20 FPS on Vita.

The Pi effect profile identifies 1122A0 as a recurring cost. Existing x87
register lowering rejects it as `slot-range`: its logical slots span -3..5,
nine names mapping onto eight physical slots. Removing the check without
changing the representation would retain stale aliases.

The opt-in `--x87-regs-physical-slots 1122A0` canonicalizes operand names,
declarations/reloads, and dirty spills modulo eight. Logical depth analysis,
join rejection, call effects, and runtime call guards remain intact. Default
lowering is unchanged; no game build enables this option automatically.
The report marks selected physical-slot plans explicitly.

Validation so far:
- `tools/test_x87_physical_slots.py` passed 16,384 cases each on host
  ASan/UBSan and Cortex-A9-targeted Pi ARM (cores 0/1). It exercises multiple
  wraps in both directions, aliases, popped slots, every initial stack top,
  bit-pattern preservation, and callees replacing all slots/status.
- Default prologue/fill/spill/context output matched the preceding revision
  across 4,200 synthetic configurations. This is not full-regeneration proof.
- Target regeneration succeeded: 613 x87 instructions, 42 sync sites,
  12 guards, logical slots -3..5. Only 1122A0 was selected.

These tests cover representation and synchronization, not the full function's
arithmetic, branches, memory writes, or real callee behavior. They do not
establish speed or gameplay correctness. Full-function differential tests and
hardware qualification are required before integration.

Private artifacts: `effect-physical-slots/host`, `arm`, `regen`, `regen.log`.
The regeneration completed (session 93762 terminal). A matching no-register
baseline regeneration completed successfully (session 81276 terminal). Output is `effect-physical-slots/baseline`
and `baseline.log`. Nothing is running on the Pi now.

Next: compare regenerated baseline with the maintained target body; build a
full-context/memory differential harness with explicit callee contracts and
representative paths, then validate actual game queries/captures before any
Vita deployment. Preserve the known-good rollback and all existing hooks.

The regenerated baseline target body is byte-identical to the maintained
point-location stage target (`baseline-identity.json`). No jobs remain active.
