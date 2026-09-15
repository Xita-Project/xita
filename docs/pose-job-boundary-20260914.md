# Candidate boundary for character pose jobs

The [campaign bridge capture](campaign-npc-observation-20260914.md) strengthens
the case for measuring character preparation. It does not establish the cost of
any one pose routine. This note narrows the dependency audit for Halo CE 3925;
no new parallel pose execution is enabled.

## Separate hierarchy math from object updates

The owned executable's `0x8DDF0` routine combines object/model lookup, animation
helpers, attachment/root handling and traversal of a node hierarchy. The ordinary
child-node range `0x8E545..0x8E58B` is narrower:

- It reads a 32-byte local-pose record: quaternion, translation and scale.
- It produces a 52-byte transform, then composes it with the completed parent
  transform. The node record's signed parent index is at offset `0x24`.
- The following traversal reads links at offsets `0x20` and `0x22` and appends
  them to the traversal list. Parent results must exist before dependent nodes
  execute. Root, attachment, scale and mirroring branches take different paths.

This suggests a native hierarchy kernel with explicit inputs and outputs, with
one hierarchy retaining its dependency order. Independent, fully prepared
hierarchies could eventually be separate jobs. It does **not** justify invoking
the entire translated object function on several threads.

## What existing replay captures establish

Three preserved original-call fixtures execute the compiled ARM pose routine
with current native math helpers. Each writes one object page and the guest stack
page: `[200, 15597]`, `[196, 15597]`, and `[202, 15597]`. These are **page indices**,
not counts of hundreds of written pages. Read/write intersections between the
three captures contain only the stack page. Their modeled ARM executions contain
19,415, 4,734 and 7,890 instructions respectively; these are not CPU cycles.

The sample is small and comes from a different scene. It does not cover all
attachments, animation states, parent dependencies or callbacks. Shared native
math counters/caches and guest scheduling also remain dependencies even when
captured guest-memory pages differ. Separate guest contexts alone would not
remove those shared mutations.

## Required prototype contract

1. The guest owner resolves objects, animation and attachment/root state in the
   original order, then supplies bounded local poses, hierarchy links and root
   transforms. Workers must not retain mutable guest pointers after the loan or
   snapshot lifetime ends.
2. A pure kernel computes into separate output storage, preserving hierarchy
   order and floating-point behavior. Shared guest/native counters, callbacks
   and allocator mutations remain with the owner.
3. The owner joins before the first consumer and publishes results in the
   original order. Cyclic or invalid links, unsupported root/attachment states,
   aliases and unavailable workers retain the serial path.
4. Differential tests compare matrices, touched memory and observable guest/FP
   state against the owned translated range. Delayed workers and immediate
   source changes after completion must not affect retained output.

Before implementing asynchronous scheduling, measure the complete serial kernel,
input gathering, output publication and dispatch/join costs. Existing native
matrix/quaternion helpers already remove substantial translated arithmetic.
A worker is useful only if whole-frame time improves after these costs, with
unchanged gameplay and rendering.

This audit uses the supported executable SHA-256
`4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae`.
Original instructions, fixtures and generated sources remain in private
validation storage, outside the repository.
