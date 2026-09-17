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
traffic or performance.

## Physical campaign follow-up

The cumulative package from `1823407` is installed in updater slot 1; runtime
SHA-256 is `31af4cf390bd5f58d58429d3902f6107174de6803777d09dd1ada3d8ffc84812`.
Only the runtime and boot record differ from its parent package; all four guest
archives are byte-identical. The parent remains in slot 0 for rollback. All
five startup selections remain enabled, with scene census compiled OFF.

After a full process restart, the same Pillar of Autumn checkpoint now admits
60 final continuations per 60-frame report, retaining the 60 first stores.
There are no stencil declines in these settled windows. This confirms that the
previous exclusion was overly broad for this view. The reported 31,334,400
logical samples equal one 960×544 continuation per frame; they are not measured
memory traffic. All 60 query prefixes are still observed before final completion,
with no fallback in the recent windows.

Ordinary gameplay observation counted 767 displayed frames in 60.068 seconds:
**12.769 FPS**, versus **12.660 FPS** for the parent and **12.748 FPS** for the
earlier four-change package. The logged view and settings match; effective CPU
and GPU clocks remain 444 and 222 MHz at native 960×544. Live AI varies, so the
roughly 0.11 FPS difference from the parent does not establish a practical gain.
The built-in benchmark was not invoked, and no input or capture occurred within
the observation window.

Checkpoint images, short movement, left/right camera input, pistol fire and the
pause menu remain intact. No fault marker appears in the captured campaign
log. This is limited gameplay validation, not proof of combat/driving stability
or resolution of the historical rocket/plasma GPU crash. The refinement stays
in the cumulative candidate; stable representative 20 FPS remains unmet.

Evidence is private under the engine-restructure validation directory's
`direct-cluster-query/depth-store-stencil/`: `installation.json`,
`campaign-passive/result.json`, `campaign-analysis.json`, logs and screenshots.
