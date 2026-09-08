# Retained triangle capacity — September 6, 2026

The cryo room exceeded the frame buffer that preserves triangle indices until
GPU completion. One recorded draw needed 2,229 indices with 130,376 of the
131,072 slots already occupied. Later small lighting/effect draws filled the
remaining slots. Thirteen draws were dropped in the stationary cryo view;
camera movement changes both demand and which draws arrive after the limit.

The frame pool is now 393,216 16-bit indices per frame, costing an additional
1 MiB across the two frame buffers. The existing ownership/fence rule and
cached index-copy path are unchanged. Over-capacity draws still fail safely.
This fixes a demonstrated capacity limit; it is not a claim that every source
of geometry spikes or pop-in has been resolved.

The old log called all these failures a full command list, even though only
about 330 of 2,048 commands were recorded. Drop diagnostics now distinguish
commands, indices, persistent attributes and immediate vertices. They include
capacity and requested-index counts, and rate-limit repeated shortage messages
to avoid logging every frame. `XV_LOG_DRAW_DROPS=1` also samples frames without
drops so busy scenes can be checked for headroom.

Validation so far:

- `make -C recomp/host test-draw-prep` passes, including the recorded cryo draw,
  prior-frame preservation, final aligned allocation, and safe overflow.
- The actual retention/cache host test passes AddressSanitizer and
  UndefinedBehaviorSanitizer.
- The Vita build passes. The cryo view requests 133,704 indices. Later opening-cinematic
  samples reached 277,344 indices, exceeding an intermediate 262,144-slot
  candidate; the final 393,216-slot capacity accommodates that measured peak. The final pool passes a full opening cinematic and cryo return: the sampled
  peak is 279,256 indices, with no logged drops. This run used isolated shader
  experiments; the command ownership path is unchanged. Camera-turn/Blood Gulch
  measurements and hardware checks remain pending.
