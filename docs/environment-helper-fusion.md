# Environment helper fusion experiment

September 26, 2026. Offline only; **not installed on the Vita**.

The perf257 particle split measured 57810 at approximately 1.2–1.8 ms/frame
inclusive. This experiment asks whether eliminating its helper call boundaries
is useful before writing a native implementation or adding a cache.

`tools/test_environment_fusion.py` extracts the current private 571F0, 580D0 and
57810 bodies. It emits a reference copy and a candidate with the first two
helpers forced inline into the third. No guest operations are removed or
rewritten. All emitted game code remains in the private output directory.

```sh
python3 tools/test_environment_fusion.py STAGE/recomp/code_010.c \
  --output PRIVATE_OUTPUT --extra='-fsanitize=address,undefined'
```

The synthetic fixture checks full guest memory and context for 1,000 cases
covering missing/direct environment leaves, default vectors, differing x87
TOP/status and output spanning a page boundary. It aborts if it reaches the
dynamic material/plane or active-vector external calls. Those paths are
**unverified**, not silently assumed correct. The baseline uses normal compiler
inlining decisions, rather than being artificially marked `noinline`.

Both host ASan/UBSan and Cortex-A9-targeted ARM runs on the Pi passed the covered
cases. The Pi ran on core 0, with no other Halo harness observed running before
the test. Its four alternating-order repetitions per scenario measured:

| Synthetic scenario | Reference median ns/call | Fused median ns/call | Reduction |
| --- | ---: | ---: | ---: |
| Missing cluster | 136.10 | 129.65 | 4.7% |
| Missing environment leaf | 232.50 | 216.65 | 6.8% |
| Direct leaf/default vector, first flags | 262.25 | 239.65 | 8.6% |
| Direct leaf/default vector, second flags | 262.70 | 240.55 | 8.4% |

These times include copying the initial context for each invocation and use
warm synthetic data. Sanitized host timings are not used as speed evidence.
Pi timings do not predict Vita FPS or prove actual a30 branch coverage.

The ARM fused entry occupies 11,334 bytes versus 1,818 for the reference entry;
the reference helper symbols occupy another 4,496 bytes combined. A production
installation retaining the shared helpers would add code, potentially hurting
instruction-cache behavior. No size reduction or whole-frame gain is claimed.

Decision: keep this as a reproducible experiment, not a deployed optimization.
Its narrow modest gain and code growth do not justify another hardware update
without realistic branch coverage and a smaller specialization. The larger
collision, impact-triggered work and simulation costs remain higher-value
targets. Private artifacts: `environment-fusion-candidate/{host,arm}` and
`summary.json`, alongside the source checkout.
