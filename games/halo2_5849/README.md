# Halo 2 executable identity and initial discovery

This is an **identity and discovery profile**, not a working game port. It
selects no game adapter, function overrides, extra roots, variables or symbols.
It does not enable a Halo 2 runtime build, rendering, audio or gameplay.
`5849` identifies the linked XDK library build, not an asserted game build number.

The [initial inspection report](../../docs/halo2-initial-profile-20260912.md)
records the executable identity, validation results and known discovery gaps.

Use your matching local executable and keep all generated files in an ignored
per-game directory:

```sh
mkdir -p local/halo2_5849
halo2_input=/path/to/your/default.xbe
halo2_output=local/halo2_5849/recomp

python -m recompiler "$halo2_input" --profile halo2_5849 --check-profile
python recompiler/xbe_parse.py "$halo2_input" --json > local/halo2_5849/manifest.json
python -m recompiler "$halo2_input" --profile halo2_5849 \
  --no-data-roots --files 32 -o "$halo2_output"
```

The existing `tools/recomp.sh` wrapper selects Halo CE explicitly. Use the
module commands above for this profile. `--no-data-roots` disables scanning
non-code sections for roots; the discovery engine still follows direct calls
and considers code-address immediates. Its output contains false boundaries
and unsupported instructions and is not ready to execute.
