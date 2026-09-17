# Captured collision solver boundary

Status: implemented as an **off-by-default experiment**, tested on the host
and compiled for Vita ARM. It has not been installed or benchmarked on hardware.
This is not evidence of an FPS improvement or of independent object updates.

## Boundary found

The sampled campaign lock holder is `4C980 → 4B9D0`. The latter reaches
`49600 → 172BF0`, where two distinct phases already exist:

1. `171F10` gathers collision shapes into the current worker's guest stack.
   It reads objects and BSP data and modifies shared visitation/list state.
2. `170C10` computes motion against that captured packet, with a direct-call
   closure of eleven functions. Its shape tests consume inline spheres,
   capsules and polygons. A polygon's `+24` field is a vertex count, not a
   pointer to borrowed world geometry.

The packet is `0xAC08` bytes: an eight-byte header and three arrays with
capacities of 256 records each. Their strides are 28, 40 and 104 bytes.
Polygon vertices are inline; each polygon can contain up to eight 2D vertices.
Solver output consists of two vectors and up to sixteen 44-byte contacts.

## Implemented experiment

`XV_OBJECT_SOLVER_EXPERIMENT=1` adds a signature-checked entry/cleanup scope to
the original translated `170C10`. The original arithmetic, register effects,
guest stack, contacts and control flow remain intact. Ordinary builds omit the
scope and the kernel implementation. Even an experimental build starts disabled;
there is no environment default, dashboard setting or remote benchmark selector
that turns it on. Only an explicit drained-owner API call enables the prototype.

Admission requires:

- The actual native worker, its live context and job marker, and exactly one
  enclosing math-lock scope. Owner services borrowing a worker context decline.
- The checked `172BF0` caller and its sixteen-contact ABI, forward string
  direction, and at least 65,536 remaining backedges. The bounded closure stays
  within that budget without entering the owner scheduler.
- Unchanged private stack mappings for scratch, arguments, both input vectors,
  the entire packet, both output vectors and contacts. All these spans must be
  aligned and disjoint; shared inputs, outputs and aliases retain the guard.
- Bounded shape counts, polygon axis/direction selectors and vertex counts.
- Original image mappings and canonical math constants, projection table and
  default-vector pointer/value. No mutable world pointer is read by the solver.
- Hold sampling disabled, since a sampled child scope cannot span suspension
  of its outer guard.

Accepted calls release the enclosing lock temporarily. Cleanup reacquires it
through the existing park-aware owner-service protocol and restores the exact
depth expected by the caller's original cleanup token. The ordinary callback
guard remains in place before and after the solver. Nested or unsupported calls
use the original guarded path.

## Validation and limits

The owned-image test checks twelve complete signatures: all eleven solver
functions, its switch table, and the caller. Each unhooked emitted function must
match the installed diagnostic's staged body exactly. Mutating any checked
span suppresses the hook. Generated game code remains in private validation
storage, outside the repository.

The production worker-pool fixture executes the original solver under a guard,
then through a private scope, then through the actual emitted entry/cleanup hook.
It compares complete guest contexts and the 65 KiB scratch/input/output region,
including confirmation that captured geometry and inputs remain unchanged.
Inputs cover stationary and moving cases with empty, sphere, capsule, polygon,
mixed and maximum-capacity packets. These are synthetic packets, not a gameplay
equivalence proof.

Each configuration executes 600 callbacks, with two workers, one worker or the
owner alone and with both contention-wait strategies. The fixture also exercises
owner-service parking, nested rejection, changed page mappings, aliases, bad
constants, insufficient instruction budget, disabled mode and hold sampling.
A deliberate short delay makes overlapping private scopes observable; its
reported overlap is a synchronization test, not a speed measurement.

Host, ASan/UBSan and TSan checks pass. The real hooked closure and kernel compile
with the Vita ARM compiler, and the ordinary kernel object contains no solver
symbols. These results qualify the tested scope mechanics and computations;
they do not establish the independence of the surrounding game transaction.

## Remaining integration work

`4B9D0` writes actor state before calling `49600` and keeps actor pointers and
cached values alive afterward. Releasing the guard in the solver lets another
object callback observe that intermediate update. Private solver memory alone
does not establish actor lifetime, read/write ordering or safe publication of
the resulting movement. This is why the new scope remains disabled on the Vita.

The independent retained-state audit supplies a concrete counterexample. Actor A
writes `+18/+1C` at `4C2F7/4C2FA`, then publishes the complete vector at
`4C5D9/4C5E2/4C5EC` after solving. Actor B's contact selector reads another
actor's `+18/+1C/+20` at `4A6D0/4A6DA/4A6E4` and publishes the selected handle
at `4A73D`. A private test of those original instruction blocks selects handle 2
with A's intermediate vector and handle 1 with A's committed vector. It passes
ASan/UBSan. This is a synthetic local dependency witness, not evidence that this
contact pair occurred on hardware or a complete callback replay. Revalidating
only A or its solver packet cannot undo B's observation.

Retain the actor transaction. Prioritize cheaper collection within that guard.
Any future overlap should operate on private inputs/results or pinned immutable
geometry, without actor/list/epoch dereferences, and join before ordered
publication. A waiting worker helping with such pure inner computation needs
separate task storage and must retain owner-service parking responsiveness.
Serializing a conflicting actor's entire callback restores the dependency but
does not itself provide useful parallel collection.

The [movement follow-up](object-motion-profile-20260916.md) identifies concrete
pre-solver actor writes and adds sampled collection/solver attribution before
selecting the next implementation boundary.

The physical campaign view measured with diagnostic `c8f54f0f873c5b0c` remained
about 11.5–12 FPS. The subsequent [audio startup fix](object-stream-start-20260916.md)
is installed as `847524f8…`; the solver experiment remains compiled out. Stable
20 FPS in representative campaign and Blood Gulch driving, and resolution of
the reported GPU crash, remain unproven.
