# Guarded black-material GREATER specialization

Status: opt-in runtime candidate, host/Pi checks passed; not hardware-qualified.

The retained axis/black material optimization folds a proven RGB-zero placeholder
and exact captured constants. Its generic-alpha variant still implements all
alpha functions dynamically. For shader 154066FD's selected 7F/t8 path, the new
variant replaces that block only when the captured draw enables GREATER testing.
It retains `if (!(saturate(out_a) > xv_atest.x)) discard;`, including rejection of
NaN alpha. Alpha reference remains the captured uniform; color/alpha arithmetic,
blend state, depth writes, and draw order are unchanged.

`XV_MATERIAL_BLACK=2` enables this additional selection; level 1 retains existing
behavior. It still requires the captured texture/constants proof and the exact
selected material variant. Other shader keys and alpha policies retain their
previous paths. Link-cache mode 6 has distinct identity; its shader policy is
GREATER (2). Load failure falls back to generic-alpha black material, then the
ordinary shader if necessary. Missing optional embedded programs are supported.

Host sanitizer tests cover every alpha function, enable bit and 8-bit reference,
proof failures, alpha-disabled selection, other shader keys and other alpha
specializations. The actual production linker is extracted into the GXM mock
fixture to check distinct caching and failed-load fallback; it also passes on Pi.

`specialize_ps_black.py --greater` generates the new source from an owned generic
material shader, first validating the existing alpha block. Its output exactly
matches the previously compiled private candidate (SHA256
61d82e0e3106f1ace794d6474bff6c2d01b2d258210b90a626bf684fba05a48d).
The compiled 1,228-byte GXP preserves the original capability word; SHA256
35f7f7900f1c3415c5ff62af9f1884e0cbb9b4999927408389fad976b501978e.
Generated shaders and compiled programs stay in the private build stage.

Next: finish the independent perf246 CPU gameplay observation, then qualify the
shader on hardware. Compiler instruction counts are supporting evidence, not
proof of reduced physical Vita frame time or rendering equivalence.
