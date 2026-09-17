# Read-only stencil continuations

The fresh-launch campaign retest found no depth-store admissions because the
original proof rejected every enabled stencil test. The proof now admits two
read-only cases: a zero stencil write mask, or KEEP for all three possible
outcomes. Enabled function and operation indices must be below eight. Replay
binds the captured settings to both faces, so the proof covers both faces.

This changes only store eligibility. Stencil testing, masks, references, depth
loads, draws, shaders, query notifications and resource ownership are unchanged.
The whole continuation still rejects any depth write, possible stencil write,
clear, unproved shader or ordinary UI batch. The final continuation still needs
the existing external UI-tail proof. In particular, a read mask of zero is not
a write mask of zero, and KEEP on the pass outcome alone is insufficient.

`tools/test_depth_store.py` passes with an independent 8-bit stencil operation
and write-mask model replacing the old enabled-means-write fixture. It checks
605,088 stencil identity/admission cases and 600 production replay comparisons
per configuration. The replay comparisons cover queued/synchronous submission,
query support on/off and sanitizer builds. They preserve stored depth/stencil,
color/query outputs, ordered events, notification values and error drains.
New cases cover enabled read-only intermediate/final returns, each writing
outcome, partial masks, a later clear, depth write and shader depth export.
Existing negative cases now specify actual stencil writes rather than merely
enabling zeroed KEEP operations. Startup/controller tests and the 1,164-program
owned shader metadata check also pass.

Independent source review found no additional stencil writer: the same captured
state is bound on both faces, and clear/UI paths remain excluded or separately
proved. These checks establish the bounded read-only policy, not driver memory
traffic or performance. Hardware admission, visuals and benefit remain pending.
