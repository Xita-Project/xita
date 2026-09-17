# Separating movement, collision collection and solving

The earlier physical campaign sample attributes 90.7–94.8% of the sampled
`4C980` hold elapsed time to `4B9D0`. That is a fraction of a sampled lock hold,
not a whole frame. It does not establish that the captured solver itself is
expensive enough to justify changing object ordering.

## Shared-state finding

The original `4B9D0` writes the current actor's `+24/+28/+2C` fields at
`4BF85..4BF92`, `+18/+1C` at `4C2F7..4C2FA`, and `+464` on several branches
before calling `49600` at `4C4AB`. It retains the actor pointer in ESI and the
definition pointer in EBP across the call. Afterward it updates actor fields
including `+42B/+42C`, invokes position/list helpers `8C9D0` and `8E970`,
and publishes additional movement fields.

In contrast, `49600` receives the caller's stack packet in EBP. Writes through
that register are packet writes, not automatically actor writes. Its contact
results can also identify other objects. It calls `172BF0`, which collects
shapes through `171F10` before solving through `170C10`.

Releasing the solver's enclosing guard introduces an additional observation
point in the actor update. This does not prove a crash would occur, but private
solver buffers alone cannot qualify the surrounding transaction. The solver
experiment remains disabled. Parallel execution needs a capture/compute/commit
contract or conflict exclusion for retained actors.

## Narrow hardware attribution

The optional 1-in-64 hold sampler now records six nested function intervals:
`478D0`, `49600`, `172BF0`, `171F10`, `170C10` and `1721B0`. Hooks run only
inside an already sampled direct child on its actual native worker. They do not
read or modify guest state and retain the existing guard. Borrowed contexts on
the servicing owner, recursive instances, unsampled calls and calls outside the
child decline. The diagnostic defaults off and uses the existing `object-holds`
off/on/off comparison; it adds no gameplay setting.

Reports are inclusive and nested. Do not add their times together or treat a
percentage as an FPS gain. Scheduling, clock-call overhead and owner-service
parks are included. The comparison measures observer cost in the same view.
Joined reports reset both worker lanes' samples.

If collection dominates, prioritize world query data and traversal; if solving
dominates, prioritize the pure arithmetic and qualify parallel publication.
Work elsewhere in `4B9D0` remains visible as time outside these intervals.

## Validation

Host worker tests, ASan/UBSan and TSan pass with two, one and zero workers and
both wait modes. They exercise nested timings spanning real owner handoffs,
recursion and wrong-context rejection, guest-context preservation, containment
of elapsed intervals and report reset. The ordinary worker suite also passes.

Owned-image checks cover six complete signatures, reject modified/unsupported
images and verify every original emitted function remains byte-for-byte intact
after removing only its diagnostic entry. The two affected translation units
preprocess identically to the installed build when timing is compiled out.
The separate solver experiment is excluded from the candidate.

The native build passes. Package verification confirms unchanged assets and
launcher, with only the executable and boot marker differing. Only the two
instrumented translation units and worker bridge change in the linked objects.
Runtime SHA-256: `3e49cf6f1462c6352403f2787025448dc7a5b952125a947ad5f9f81ca13c8782`.
Physical attribution is pending. No performance improvement is claimed for this
measurement change.
