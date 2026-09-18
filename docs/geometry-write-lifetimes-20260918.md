# Geometry write boundaries and retained pointers

The next cache optimization must distinguish map-loaded BSP/model resources
from temporary geometry. The original executable retains writable temporary
vertex pointers, and its index-pointer helper reads resource Data without a
lock call. A policy that invalidates only when `VertexBuffer_Lock` runs would
miss subsequent writes through retained pointers. These findings rule out that
blanket policy; they do not demonstrate that the separate map-loaded vertices
are modified during a rendering pass.

The existing snapshots and exact validation remain in place. This audit changes
no runtime behavior and establishes no FPS improvement.

## Reproduce the evidence

Use the owned Halo 3925 executable and the generated source for the candidate
build. Python dependencies are `iced-x86` and, for execution checks, `unicorn`.
Keep detailed reports local:

```sh
python3 tools/audit_halo_geometry_lifetimes.py \
    --xbe /path/to/haloce/default.xbe \
    --generated /path/to/build/recomp \
    --output /private/path/geometry-lifetimes.json
python3 tools/test_halo_geometry_lifetimes.py \
    --xbe /path/to/haloce/default.xbe
```

The audit pins the executable SHA-256, independently decodes eleven complete
instruction intervals (406 instructions), checks 23 selected call sites and
records memory-write operands. Generated comments only nominate additional
direct calls; each nominated call is checked against the XBE. Repeated comments
and overlapping lifted functions count as one original guest call site, with
their owner aliases retained. No game bytes or disassembly are emitted.

The qualified stage has four unique direct vertex-lock sites, four vertex-buffer
creation sites and seven resource-registration sites. These are static code
counts, not runtime frequencies. No direct call to `0x7A9D0` was found in these
generated comments. The follow-up identifies its byte-identical 53-byte copy
at `0x623F0`, with seven verified direct callers. Four callers immediately use
its output in the visible-index building path: `0x5429F`, `0x5447A`, `0x5452E`
and `0x547FE`. This connects the direct Data access to actual rendering code,
but does not establish its frequency in the measured scenes. Indirect calls,
unlifted paths and writes through aliases remain outside this inventory.

## Resource families

| Family | Registration or allocation | Writable-pointer behavior | Boundary needed before caching |
| --- | --- | --- | --- |
| BSP render/lightmap resources | `0x33860`, reached from BSP loading at `0x3555F`; two 12-byte resource arrays at header `+4/+8` and `+0xC/+0x10` | Registration fixes resource headers; it does not prove payload immutability | Loaded BSP generation, complete payload writer coverage and GPU retirement |
| Model resources | `0x33930`, reached at `0x3547E`; vertex array at `+0x10/+0x14`, index array at `+0x18/+0x1C` | Vertex resources call Register; index resources set Common directly | Separate model/map lifetime and payload writer coverage |
| Temporary vertices | `0x7A810` allocates pools; `0x7A6D0` reserves records | `0x7A630` passes the address of record `+0xC` to Lock and returns the saved pointer | Continue owning snapshots unless the complete writable interval is known |
| Temporary indices | `0x7A810` creates the buffer; `0x7A740` reserves ranges | `0x623F0` and its identical copy `0x7A9D0` compute `Data + first_triangle * 6`, save record `+8` and return it without a lock | Pointer writes and camera-dependent index generation must remain accounted for |

The vertex pool table at `0x278AB0` has 20-byte records; its allocation table at
`0x278BA0` has 16-byte records. Successful reservation writes format, start and
count, but leaves the old `+0xC` pointer intact. `0x7A6D0` is a reserve operation,
not an unlock or a completion boundary. Its capacity comparison is strict:
exactly filling the remaining capacity is rejected. The index allocator has the
same strict capacity behavior and also retains its old pointer field.

Original x86 execution with synthetic pool state confirms these behaviors in
24 vertex-reservation cases, six index-reservation cases and 56 direct-pointer
cases across both entry points. No HLE or GPU timing model is involved in those leaf executions. Five
invalid audit inputs are rejected, including a changed executable, a wrong call
target and a truncated instruction interval. A duplicate-comment fixture checks
that lifted aliases do not inflate the call count.

## Retirement is a separate requirement

BSP drain `0x337F0` and model drain `0x338D0` call
`D3DResource_BlockUntilNotBusy`. The current implementation in
`recomp/kernel/xd3d.c` returns immediately. It is not a GPU fence for a future
direct guest-memory path. The pool release routine `0x7A7B0` releases and clears
the twelve pool handles, alternate vertex buffer and index buffer; it likewise
does not revoke pointers already held elsewhere in guest state.

Current `VertexBuffer_Lock` returns an alias of resource Data plus the requested
offset. Tracking that HLE call records pointer acquisition, not the lifetime of
writes through the result. The direct index helper also shows why lock history
cannot be a general resource-write census.

## Next implementation boundary

Continue with BSP and model payload writers separately. Enrollment immediately
after their registration boundaries can establish resource provenance and a
new load generation. It cannot by itself authorize skipping byte validation.
Follow retained descriptor pointers, bulk copies, file reads and physical aliases
to establish either a complete invalidation policy or a narrower read-only
render interval. Keep exact validation for temporary and unclassified sources.
Any native cache must still retain old allocations until their GPU users retire.

The earlier [hardware range captures](geometry-ownership-20260917.md) matched
over 99.8% of requested bytes to map-loaded BSP/model resources in two Blood
Gulch views. That makes this narrower audit worthwhile; it is not evidence that
all those bytes can already bypass validation. The cumulative visibility and
compact-capture candidate remains ready for ordinary hardware testing. This
audit does not replace or rebuild that qualified package.
