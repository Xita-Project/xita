# Exact indexed checks for retired raw vertex snapshots

This default-OFF candidate extends the existing eight-record indexed equality
check to the retired-slot residency branch. Previously only current-frame cache
entries used the captured reference mask; residency compared every raw byte.
Packed06/29/58 requests and all ineligible raw requests keep the existing path.
No guest source is treated as immutable and no shader/fetch layout changes.

For each admitted request, the slot is already retired, its initialized cached
mirror and GPU bytes agree, and the existing mask from the **same retained index
list** covers every fetched vertex. Every attribute must lie within the validated
stride. Exact equality of all referenced groups is sufficient to retain the old
payload. Unreferenced records can remain old in both copies. A later draw still
validates its own mask/full span; a newly referenced mutation appends a new full
snapshot. The mirror alone is never refreshed. Index-mask metadata bounds cannot
prove an arbitrary fabricated bitmap covers the indices; this API retains its
existing trusted-mask construction contract.

Allocation limits, source loans, append order, source/layout keys, initialized
padding, CPU copy tickets, dirty envelopes, three-slot ownership and final GPU
retirement are unchanged. A sparse clean tail must keep an earlier pending ticket,
including zero after wrap. Failed referenced equality retains the original full
snapshot path. New requests do not skip comparisons by resource pointer, Lock
history, page identity or a claim that BSP data is static.

## Startup selection and build ownership

`XV_VERTEX_RESIDENT_REFERENCES_DEFAULT=0` remains the repository build default.
Use `=1` in a reviewed cumulative build to select it at process start. Only
`runtime/xv_vertex_upload.o` receives this macro; the
`build/vertex-resident-references-startup.config` content stamp rebuilds that
object when the setting changes. Explicit `XV_VERTEX_RESIDENT_REFERENCES` in the
environment/config handoff wins, preserving the uploader's existing `atoi`
convention: empty/nonnumeric/0 disable, negative/nonzero enable. There is no live
control/benchmark selector. Residency and indexed coverage must also be enabled
through their existing settings for the new branch to admit a request. A compiled
startup default0 is not a compile-out: raw residency evaluates the cached setting
and adds a branch; no per-request clock, allocation, lock or worker is added.

The existing bounded upload report now includes `[vertex-resident-references]`:
selected mode, admitted retired raw checks/hits/runs, full requested span KiB and
actual comparison-run requested KiB. These are not memory-bus traffic or CPU/GPU
time; a failed run can exit before visiting its suffix. Existing resident and
upload comparison totals now include those run spans instead of the full span
for this new branch. Historic residency comparison totals therefore are not
directly comparable to enabled sparse totals. Avoided-snapshot bytes still describe the omitted full
snapshot payload, independently of actual queued/caller GPU writes.

## Qualification

`python3 tools/test_vertex_resident_references.py --output-dir PRIVATE_OUTPUT`
checks 30 fresh processes with macro absent/0/1 and explicit environment choices,
including actual production upload paths and selected-mode report. It also runs
six real-Make builds: absent→0→1→1→0→0. Only the uploader rebuilds on a changed
value; main/D3D/shader/UI and the generated guest archive stay unchanged. Invalid
Make values and invalid numeric C defaults are rejected.

The focused actual uploader/real copy-worker fixture
`tools/tests/vertex_resident_references.c` covers 20 enabled cases (15 disabled),
including three-slot delayed UINT_MAX/0/1 tickets, retired sparse hits,
unreferenced changes followed by a same-source newly referenced/full request,
referenced changes, separate source addresses and same-pointer aliases, mixed
packed/raw layouts, odd strides/padding, metadata-ineligible/absent/dense masks,
residency disabled and worker startup/signal failure. It independently checks
fetched records against retained indices and verifies mirror==GPU after joins.

Private evidence under `direct-cluster-query/vertex-resident-references/` includes
four ASan/UBSan runs for packed OFF/ON and mode OFF/ON; existing production upload,
indexed-reference and actual-write dirty-envelope suites under ASan/UBSan; the
new actual-worker fixture under TSan; and two rejected compiled mutants (ignore
fetched changes, refresh mirror only). Both startup-default ARM uploader objects
compile. No full game build or hardware/emulator execution was done here.

These fixtures emulate Vita memory/copy services with host allocations/pthreads.
Final GPU retirement remains the original caller contract, represented by manually ordered
slot reuse and preservation of other retained slots in fixtures; this is not a new proof of the whole frame pump
or arbitrary remapped guest page tables. Diagnostic full-span dumps can contain
older unfetched bytes, just as existing selective current-frame reuse permits;
this optimization guarantees fetched-record equality, not byte identity of
unfetched data.

## Coverage limits

Current saved C deep-valley frame 5040 has 133 residency checks and 625.05 KiB/frame
of retired comparison requests within 12.263 ms stream elapsed/25.7 ms full draw-HLE.
That 625 KiB includes packed and dense requests and is only a loose upper bound;
no existing counter measures new-branch eligible coverage. Same-frame sparse
traffic is only 0.483 checks/frame there, versus 2–8 in other selected V views.
Different views cannot establish a performance comparison. This candidate may
have little coverage in the expensive valley view; no saved measurement proves
recoverable milliseconds or a whole-frame gain. The source audit and exact
selected log windows are preserved in `remaining-stream-preparation-audit/`.
