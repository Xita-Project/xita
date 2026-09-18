# Copy only existing hierarchy parents

The [hardware resolution comparison](hardware-resolution-critical-path-20260918.md)
leaves about 17 ms/display frame in joined object batches in its stationary
view. The retained hierarchy batch runs about 31 times per display frame there.
This change reduces copying within that existing batch; it does not move game
updates to new threads or remove their shared guard.

After validating the complete parent-before-child worklist, the helper formerly
copied all `count` old output matrices into local storage. Only the `first`
already-completed nodes can supply an old parent. Every subsequent node is
computed before any child reads it. The helper now copies and validates those
prefix matrices by their actual node IDs, including shuffled IDs. Pose snapshots,
range and alias validation, arithmetic, speculative-failure handling, original
final iteration, publication order and scheduler budget are unchanged.

This removes `52 * (count - first)` bytes of old matrix copying per accepted
batch. For eight nodes with one completed root, that is 364 bytes. It does not
claim the whole helper gets faster: indexed prefix copies add instructions and
their tradeoff depends on the hardware cache and model size.

## Validation

- Host comparisons against the independent original-region lift and existing
  native-leaf path: 201 full context/arena comparisons in each of enabled,
  unset, disabled and global-math-disabled configurations. The same suite passes
  ASan/UBSan. Twelve unchanged-decline fixtures pass in each configuration.
- Nine added cases fill every unfinished old matrix with signaling-NaN bits.
  Chains, trees, shuffled node IDs and consumed prefixes still match the
  original. These detect accidental reliance on old matrices that should be
  overwritten.
- Vita-compiled ARM differential checks: 144 fixtures compare the preceding
  helper and complete hooked loop with the candidate. Full guest context,
  arena, FPSCR, scheduling yields and admission/counts match. Counts 8/32/64,
  three node orders, prefixes 1/2, four rounding modes and normal/FTZ/default-NaN
  control combinations are covered.

The ARM instruction model excludes the implementations of memory-copy imports.
It reports 35 additional instructions for one prefix node and 74 for two in
the sampled chains, while reducing imported copy traffic (the fixed 52-byte
copies are inlined). This is not a cycle estimate or proof of an FPS gain.

Private original lift, baseline source, ARM build/comparison script and results
are in `2026-09-18-unified-games/hierarchy-prefix/` under the local backup root.
Hardware performance and heavy-gameplay acceptance remain unverified.
