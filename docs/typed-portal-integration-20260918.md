# Guarded native portal clipping

The visibility traversal can now use a typed native polygon operation at its
audited `0x534D5` call. This removes repeated emulated register, stack and page
translation work inside that operation. Traversal order, visibility outputs,
rendering settings and the existing worker schedule remain unchanged.

`XV_TYPED_PORTAL_POLYGON=1` enables the experiment; the default is off. It
requires the Halo CE 3925 profile, native portal traversal, native clipping and
the existing owner/worker admission backend. `tools/portal_polygon_hook.py`
verifies the owned executable and pins the cumulative generated caller before
preparing one guarded code unit. Other callers and interior entries retain
their implementation. Turning the flag off produces byte-identical caller and
clip-controller objects to the preceding runtime.

## Admission and state

`xk_portal_polygon.c` admits only the registered guest owner with no outstanding
worker jobs, owner pass, census or diagnostic activity. It checks the exact
caller layout, stack bounds, constants, mapped spans, relevant physical aliases,
word alignment and finite bounded coordinates. Inputs are copied into local
native arrays; no guest pointer is retained. Unsupported cases fall through to
the existing clipping implementation without changing guest state.

The adapter requires enough cooperative scheduling budget to finish without a
yield and deducts the original backward-branch count exactly. A nonfinite
intersection restores floating-point status before falling back. Successful
calls publish the output polygon, signed count, return stack pointer and budget.
Dead emulated scratch registers and physical x87 slots are not reconstructed;
the enclosing caller/consumer tests establish the narrower live-state contract.
Double intermediates and float rounding points remain. The math object uses
`-frounding-math -ffp-contract=off`.

## Qualification

- 2,048 ARM geometry cases compare counts, positive output bytes and budget
  against a fingerprinted original across 16 floating-point modes.
- 33 ARM runtime cases compare the preceding installed objects with the actual
  guarded objects. These cover noncontiguous mappings, fallback, rounding,
  crossing and empty polygons, a diamond, visible recursion depths 4/16/32,
  and the enclosing `0x53540` visibility consumer in five FP configurations.
  Every byte outside dead recursive stack memory, callee-saved registers,
  logical x87 depth, return stack and remaining budget matches. Fallback cases
  also compare complete context and floating-point status.
- The real pthread worker backend passes 33 complete-state admission checks;
  a positive owner call and 24 decline cases pass address/undefined-behavior
  sanitizers. Another 1,936 bounded native polygon calls pass these sanitizers.
- Three build transitions preserve the disabled implementation and reproduce
  the enabled objects. Ten malformed or incompatible configurations reject.

With all admission checks included, the synthetic projection traversal changes
from 27,196 to 15,003 modeled ARM instructions. A visible 16-level chain changes
from 385,846 to 195,059; the 32-level case changes from 795,678 to 401,035.
Fallback adds some admission overhead. These are instruction-model results,
not Vita cycle measurements or an FPS prediction. The native workspace is about
10 KiB, released before child traversal recurses.

The complete cumulative VPK builds successfully: 1,588 package members, with
only the runtime and its boot manifest changed. The update contract is
unchanged. Two existing objects change, two native polygon objects are added,
and the other 93 objects match the preceding build. Candidate runtime SHA-256
is `639b47fd4098dff13fef838ae7e5598858fa9de813b84d4e446347d3aa1d06ec`.

Private qualification and package receipts live in
`direct-cluster-query/typed-portal-integration-20260918`. Two authenticated
read-only device requests timed out after packaging; no update or restart was
sent. The last verified installed runtime remains `175f18da`. Hardware
deployment and ordinary gameplay verification are next. Stable 20 FPS is not
yet established. This change adds no visibility worker; parallel traversal
still needs a separate ownership plan.
