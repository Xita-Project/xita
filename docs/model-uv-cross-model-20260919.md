# Optional UV value reuse across model scopes

`XV_MODEL_UV_CROSS_MODEL=1` retains the qualified canonical 56F20 value payload
across clean selected A26B0→A2380 model exits, within an actual owner SCENE and
Present interval. The option defaults to 0 and requires `XV_MODEL_UV=1` with its
existing owner/profile prerequisites. The generic 56F20 path and selected caller
hooks remain unchanged. No hardware improvement is claimed by this integration.

## Lifetime and input contract

Only cached values survive: the effective argument/FP key, final scratch/rows,
four ST values, FSW and FPSCR. Pending guest/native pointers never survive a
model exit. Retention requires a depth-one, unblocked exit, active owner SCENE,
no diagnostic or pending cold call, and unchanged current roots, memory bounds
and packet. New model admission checks the owner/generation/root envelope and
binds its current packet under a new serial. Mid-model packet changes still
block reuse; accepting a different packet at a fresh selected model binding does
not weaken that check.

The owner's outer SCENE begin/end and every accepted Present invalidate cached
values and pending pointers. Present covers both the same-owner early return
and a newly bound owner; existing Present/Swap HLEs already call that function.
An open model is blocked until its normal exit. Owner generation alone does not
identify a frame. The new boundary function repeats real owner admission before
mutating cache/counters, and does not read clocks or change guest state.

No key field was removed. Canonical56F20 reads `[ECX+4]` (packet+88, a scalar
pointer), but zero selectors avoid all scalar-array dereferences. The pointer is
pushed at 56FF4, ECX is cleared, and 57000 overwrites the pushed word with normalized
time before any read or callback. It does not survive in output registers,
scratch or ST slots. The existing mapped input-span and physical nonalias checks
still apply; unsupported selectors still execute the original function.
Packet-derived scale results and time are already in the six-word argument key.
Descriptor/constants are revalidated at every call. Distinct cosine/sine
rounding, scratch writes and sticky/control FP state remain unchanged. The only
normalized incoming bits remain the previously qualified FSW 0x4700/FPSCR-NZCV.

## Joined counters

When enabled, `[model-uv-cross]` reports `retained-exits`, `cross-scope-hits`, and
`boundaries`, next to the existing joined UV row. A cross-scope hit counts only
the **first** retained hit in a new model; subsequent intra-model hits do not
inflate the number of avoided cold computations. Reporting resets counters
without discarding a valid payload. No per-call timers were added.

Perf15's last 12 checkpoint windows had 7,991 cold computations and 13,021 hits in
22,397 calls over 720 frames. Existing zero argument/FP misses do not establish
cross-model equality: the old scope lifetime cleared the cache before it could
compare successive models. Live new counters are needed to establish coverage.

## Validation

- 233 exact-production ARM comparisons with the option enabled: 111 existing
  helper/owner/clean-transition cases, 78 focused cross-model/counter cases,
  24 generated callsite/publication cases and 20 callback lifecycle cases.
- 111 default-off comparisons; a clean next model remains cold. Default and
  explicit 0 helper/owner code/data/relocations also match pre-change 35de910.
- 2,374 existing normalized-condition-key comparisons against the final ON ELF.
- Nine real Make/archive transitions cover base UV and cross-model on/off/noop
  changes, default 0, invalid values and prerequisites. A cross-model-only toggle
  rebuilds only the helper and owner objects; it does not regenerate guest code.

State checks compare the entire 360-byte context, 8MiB arena and raw FPSCR against
actual retained 56F20/173F20 plus libm. Cases include relocated packet/material/
stack/output addresses, poisoned old storage, unread unmapped scalar pointers,
changed live keys/program/constants, physical aliases, in-scope root changes,
invalid exits, nested reentry, pending cold work interrupted by Present, foreign
owners/workers, and actual SCENE/Present boundaries. A synthetic five-model
sequence naturally carries original FP state and hits on all five transitions.

Retained 70110 front/back callsite regions run real SetVertexShaderConstant;
publisher-entry state and final xd3d_state match. Callback cases perform clean
model exit, Present, SCENE restart, nested model reentry or packet mutation
before the next model. Native identity and memory discovery are controlled
fixture inputs, while owner admission and boundary functions are actual code.
Full A2380/70110 rendering, real scheduling/concurrency and physical GPU execution
are outside this fixture. Synthetic matching keys are not an asset trace.

## Cost and remaining measurement

Modeled ARM instructions are not cycles/FPS. Original UV 1,946; cross-model
option cold 3,301 and same-model hit 1,090. A complete cold UV+clean exit+next scope
costs 3,905 versus 3,798 with the option off; subsequent matching models cost 1,700.
Ten matching models total 37,980→19,205, with modeled owner thread/fiber lookups
40→31. Ten alternating scale keys instead cost 37,980→39,878 (~5% worse).
Actual native syscall cost is not modeled. Scene/Present boundary overhead is
additional; an illustrative ~8.7% incremental match break-even in this fixture
is not a gameplay threshold. Static frames are 216B UV begin, 56B UV end, 72B scope
begin, 32B scope end and 16B boundary. There is no full-context clone.

Use `tools/test_model_uv_cross_model.py --xbe OWNED_XBE --manifest MANIFEST
--retained-build RETAINED --out PRIVATE_OUTPUT` in the existing Unicorn/
pyelftools environment. It compiles exact production sources and saves compiler
commands, source/ELF hashes and per-case results. `tools/test_model_uv_build.py
--out NEW_PRIVATE_OUTPUT` tests the real Make graph without building the game.
The existing `tools/test_model_uv_key.py --elf PRIVATE_OUTPUT/uv.elf
--output-dir PRIVATE_KEY_OUTPUT` checks unchanged normalized-key semantics.
Local integration evidence is in `../model-uv-cross-model-production/`.


## perf18 physical campaign result

`0.2.0-perf.18 / c5c93f2+` was uploaded, hash-verified and boot-confirmed in
slot 0. Slot 1 retains perf17. It adds `XV_MODEL_UV_CROSS_MODEL=1` to the complete
perf17 stack, including index metadata. The package changes only `game-a.self`
and `boot-game.txt`. Runtime SHA-256:
`9dbc1be49118d1fc599e69c69b9bef3eec1be8d9e6e80aec474150a933dbee5a`.

Normal campaign loaded at the saved marine checkpoint with unchanged settings
and camera. The first capture includes two loading/settling windows and is not
the final comparison. A later stationary capture supplies twelve complete
60-frame windows after settling:

| Measurement | perf17 | perf18 settled |
| --- | ---: | ---: |
| Median frame interval | 78.30 ms | 78.50 ms |
| Median FPS | 12.8 | 12.7 |
| Median draws/frame | 150.5 | 154 |
| Eligible UV reuse, summed windows | 62.11% | 69.00% |
| All-call UV reuse, summed windows | 58.02% | 64.77% |
| Median index preparation | 1.7195 ms | 1.7525 ms |

Perf18 totals are 14,667 UV hits, 6,588 cold calls and 22,644 total calls.
The new row reports median 120 first cross-scope hits per 60 frames (two per
frame), 646 retained exits and 180 boundaries. Live argument mismatches now
appear; no FP-key mismatches appeared in these selected rows. Retaining values
therefore has real but limited cross-model coverage. The index-metadata option
remains enabled with no reported capacity failures in the selected windows.

These nearby ordinary-play captures vary in NPC activity/draw counts. The
0.1-FPS difference does not establish a regression or gain. The cache changes
remain cumulative research options, with project defaults unchanged. No
20-FPS claim follows from the improved reuse fraction or isolated instruction
savings. Further UV lifetime tuning has a modest observed ceiling in this view;
next investigate larger model-preparation costs while retaining the prior
negative material-builder cost results as constraints.

A camera turn and three pistol trigger presses completed; screenshots show the
magazine icons decrease and the scene continues rendering. All three captured
logs contain no searched STOP/FATAL/GPU-crash/trap marker. This bounded check
does not establish extended combat stability. Controls were returned to neutral.
Private artifacts and reproducible build settings are under `ce-perf18/`.
