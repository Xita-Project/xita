# Exact UV reuse within a model render pass

`XV_MODEL_UV=1` optionally reuses the common material UV calculation within one
selected model pass. It defaults Off, requires the CE 3925 profile and the owner
phase support, and preserves every original cold calculation and generic entry.
No shader approximation, draw sorting, resource lifetime extension or worker
ownership change is introduced.

The two selected calls from primary `70110` to `56F20` run inside an explicit
`A26B0 → A2380` scope. The cache is invalidated on scope exit, unsupported input,
diagnostics or nesting; nested work blocks further reuse in the outer scope.
Foreign workers, native threads, contexts and fibers are rejected before touching
mutable cache state. The owner generation, active scene, arena/image/page roots,
image bounds and current model packet must match. Packet addresses alone are not
lifetime tokens.

Each attempt reads the current canonical UV descriptor, six raw argument words,
image constants, floating-point stack position and supported FP control/status.
Physical span checks reject misalignment, page seams and aliases among outputs,
scratch and inputs. Cache hits reproduce the two rows, original scratch/argument
writes, overwritten x87 slots, live registers, lazy flags and final FP status.
The public routine and all declined calls retain the original code. In particular,
the original unrounded double cosine and float-rounded sine inputs remain exact;
a default 360-degree rotation is not replaced with an identity matrix.

Only incoming guest FSW bits `0x4700` and native FPSCR bits `0xf0000000` are
omitted from key equality. The first original comparison overwrites those bits
before their first read. Source and retained ARM disassembly confirm this.
All admitted cumulative exception flags, rounding/FZ/DN settings, remaining FSW
bits, TOP and FCW remain keyed. Unsupported states, including native QC, decline.
Replay does not normalize, erase or guess outgoing status bits.

## Qualification

- 109 comparisons of the production helper/lifecycle against full original
  `56F20`, waveform-zero code and linked Vita math library. Complete context,
  8 MiB arena and raw FPSCR match. The real owner-phase predicate is linked;
  OS thread/fiber identity and worker identity are controlled fixture inputs.
- 24 actual generated caller-region comparisons, including production shader
  constant publication, entry snapshots and complete D3D state. Callback changes
  to live context, scale and time retain exact behavior.
- 2,374 independent production-key comparisons. These include all 16 guest
  condition-bit combinations crossed with all 16 native condition nibbles at
  every x87 TOP, all rounding/FZ/DN combinations in additional cases, and retained
  live-bit misses/unsupported-QC decline. Full context, arena and raw FPSCR match.
- Full primary `A26B0` and `70110` compile Off with original text/relocation
  identity. Six real Make/archive transitions verify selective rebuilding and
  removal of the helper when disabled. Invalid selections and missing hooks fail.

The fixtures do not execute a complete model walk, real concurrent scheduler or
GPU draw. Successive hit fixtures reseed FP entry state, so they do not measure
naturally occurring campaign hits. Private evidence is under
`model-uv-reuse-prototype/production/` and
`model-uv-normalized-key/production-final/` in the unified-games workspace.

Reproduce with owned inputs and VitaSDK/Unicorn/pyelftools:

```sh
python tools/test_model_uv.py --xbe "$XBE" --manifest "$MANIFEST" \
  --retained-build "$RETAINED_BUILD" --out "$PRIVATE_EVIDENCE"
python tools/test_model_uv_caller.py --xbe "$XBE" --manifest "$MANIFEST" \
  --retained-build "$RETAINED_BUILD" --out "$PRIVATE_EVIDENCE"
python tools/test_model_uv_key.py --elf "$PRIVATE_EVIDENCE/uv.elf" \
  --output-dir "$PRIVATE_KEY_EVIDENCE"
python tools/test_model_uv_build.py --out "$NEW_PRIVATE_BUILD_EVIDENCE"
```

Keep generated game bodies and linked fixtures outside the repository.

## Cost and hardware acceptance

The bounded production path uses 1,946 modeled ARM instructions originally,
3,298 on a cold cache call and 1,084 on a hit. That needs approximately 61.1%
eligible hits to break even in this model, before model-scope setup and real
OS/worker identity lookup costs. Instruction counts are not hardware cycles or
an FPS prediction. No physical speed improvement has been established.

Joined `[model-uv]` counters distinguish scope and first/second-side volumes,
hits, cold calls, argument-only/FP-only/mixed misses, changed FP fields and
admission declines. There are no per-call timers. The next hardware check must
confirm useful live reuse and frame behavior after a fresh launch with the
existing cumulative options retained. Descriptor similarity alone is not proof
that the optimization helps.
