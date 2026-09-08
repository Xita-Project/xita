# Offline recompilation tools

Run the instruction lifter from the repository root:

```sh
python -m recompiler --help
python -m recompiler --list-profiles
```

The [build guide](../docs/building.md) describes the full development workflow.
Installing an existing VPK uses the [installation guide](../docs/installing.md).

| Module | Purpose |
| --- | --- |
| `xita_recomp.py` | Discover and lift x86 functions into C. |
| `core/` | Profile validation, shared XDK policy, hook interfaces and output handling. |
| `xbe_parse.py`, `xbe_image.py` | Read an XBE and create its runtime memory image. |
| `dx8_shader_parse.py`, `dx8_pixelshader_parse.py` | Decode Xbox shader inputs. |
| `shader_recomp_gen.py`, `pixelshader_recomp_gen.py`, `gen_layouts.py` | Generate shader source and layout descriptions. |
| `xbe_shader_pairs.py` | Inspect shader relationships in an executable. |
| `halo_map.py`, `halo_scene_export.py`, `halo_flare_hooks.py` | Existing Halo-specific data and hook helpers. |
| `xbox_kernel_exports.py` | Xbox kernel export definitions. |

Direct script commands also work, for example
`python recompiler/xbe_parse.py haloce/default.xbe --json`.
`xbe_parse.py` and `xbe_image.py` can still be downloaded together and used for
one-time game-data preparation without installing the compiler dependencies.

Title manifests and reviewed game adapters live in [games/](../games/).
Private symbol/manifest inputs belong in `local/halo_ce_3925/`, which Git ignores.
