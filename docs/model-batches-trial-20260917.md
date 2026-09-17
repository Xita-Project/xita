# Private startup trial for model batches

`XV_MODEL_BATCHES_TRIAL=1` is an explicit private build selection that enables
the reviewed hierarchy and palette batches for the next game launch. Its default
is zero. It requires `RECOMP=1`, `GAME_PROFILE=halo_ce_3925`, and both
`XV_NATIVE_MODEL_PALETTE=1` and `XV_NATIVE_MODEL_HIERARCHY=1`.

Add this selector to the complete retained cumulative build command. It is not
a replacement for that command or a graphics preset. Only `runtime/main.o`
receives the trial define, and a content-aware stamp tracks both transitions.
No guest code generation, arithmetic helper, or shared compiler option changes.

After the dashboard returns and reloads settings, before the pump and guest
threads start, main calls the existing palette and hierarchy override APIs with
one. This deliberately selects the trial over configured batch zeros. The log
prints the merged configured palette, hierarchy, native-math and matrix-NEON
values, the requested overrides, and the effective startup selection. The
existing `XV_NATIVE_MATH=0` availability gate still disables both helpers and is
reported as such.

No environment variable, configuration file, graphics setting, or matrix-NEON
selection is changed. There is no live toggle or new remote endpoint. Existing
benchmark override/reset behavior remains intact; this selector is a startup
action, not a permanent lock on either API. Ordinary gameplay admission counters
remain the check that the selected batches are actually being used.

With selector zero, the startup call/body is compiled out. Actual ARM builds
verified byte-identical restoration of the preceding main object. Six real Make
build/no-op transitions changed only main; all other retained objects were
verified unchanged. Both native helper objects were actual requested build
targets, so this also checks the selector does not rebuild them.

Thirty ASan/UBSan fresh processes execute the actual settings loader and startup
body with the real override implementations. Cases include absent, zero,
nonzero, empty, invalid and negative configuration values, dashboard reload
precedence, native-math disable, unchanged matrix-NEON/graphics/environment/file
contents, and normal override reset semantics. Numeric and prerequisite guards
were checked in C and Make. Source sequencing verifies the only call lies after
dashboard/config handling and before both game thread creations.

The unchanged batch arithmetic and worker behavior reuse the reviewed combined
fixture in [model-batch-composition-20260917.md](model-batch-composition-20260917.md).
This startup qualification performs no device access, VPK deployment, complete
package link, or hardware timing measurement. Root owns the complete cumulative
package build and fresh-launch gameplay trial.

Reproduce the focused check with a private retained build and its original
package command receipt:

```sh
python tools/test_model_batches_startup.py \
  --retained-build "$RETAINED_BUILD" \
  --build-command "$BUILD_COMMAND_JSON" \
  --output-dir "$PRIVATE_EVIDENCE"
```

The test adds the retained ancestor-query selector when needed and preserves the
remaining package flags. It uses the real ARM compiler for incremental checks;
the fresh-process selection fixture uses host sanitizers and serial guard stubs.
