# Reducing repeated setup in world collision searches

`XV_QUERY_WORLD_RUN=1` optionally follows a short run of single-child world BSP
nodes inside the existing fused collision query. It captures the unchanged
center, radius, floating-point controls and arena bound once per run. This
removes repeated per-node setup and guest register reconstruction. It neither
reduces collision accuracy nor releases the outer actor transaction.

The option defaults Off. It requires the existing object-space query route,
ancestor-scalar path and experimental object jobs. The generator pins the full
qualified input and installs the hook only at the explicit `0x87EC1` backedge;
the first-node fallthrough remains unchanged. One shared implementation receives
an explicit world/object selector. The world wrapper enables runs, while the
object adapter bypasses them. Generic functions remain intact.

## Execution boundary

Only the actual object-worker thread, with its exact guest context and a held
math transaction, can enter the helper. Foreign/native service contexts,
inactive jobs, missing guard depth and slow guard mode decline. Admission
precedes shared counter writes. The retained actor mutex serializes admitted
workers; counters are read and reset only after those workers join.

Each chunk skips at most 32 single-child nodes. Positive descent consumes one
scheduler decrement; negative descent consumes two, matching the two original
backedges. A run stops before a scheduler callback, leaf, split or unsupported
input, and leaves the final visited node to its unchanged emitted block. That
node reconstructs the required guest registers, x87 and flags. A zero-progress
decline restores entry FPSCR. There are no callbacks or guest-memory stores
inside an admitted run, and no retained cross-query state.

The helper checks captured roots, masked FP traps, finite inputs, aligned
single-page spans and physical arena bounds. Native plane arithmetic retains
the existing order. Watch/trace modes decline. Arena size is read once per
attempt, rather than adding a function call for each node.

## Qualification and limits

The initial complete-query prototype passed 37 focused cases and 95 ordered
callback snapshots, including a rejected wrong-budget negative control. The
route/parent gate adds 21 cases and 518 ordered callback, profile and packet-writer
snapshots per lane. It compares the full context, 8 MiB guest arena, page tables,
roots and FPSCR. Its admitted long run skips 14 nodes, executes the original
final split, then reaches the real `0x868F0` continuation and surface, edge and
vertex writers. Object-route cases bypass the helper.

The fixture uses the actual object-job runtime storage and guard operations;
OS thread/mutex calls are modeled. It covers diagnostic and owner declines,
root replacement, callback mutations and all four rounding modes. This is not
a proof of concurrent firmware scheduling or the full actor/solver pass.

The long synthetic parent case used fewer modeled instructions, but short runs
and repeated declines can regress. Worker admission adds a thread-ID lookup per
attempt. Hardware activation and frame times must determine whether the option
belongs in the cumulative build; instruction savings are not an FPS prediction.

`tools/test_query_world_run.py` checks installation against private owned inputs:
default identity, query-only changes, repeated-generation no-op, Off restoration
and rejection of drift or invalid selectors. The existing query and solver
Make-graph regressions remain applicable. The joined `[query-world-run]` row
reports attempts, chunks, skipped nodes, largest chunk and declines without
adding per-node timers.
