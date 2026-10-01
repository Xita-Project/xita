# Captured glass programs — September 6, 2026

The cryo observation windows are drawn with two passes whose canonical shader
keys were missing from the packaged table. An on-demand frame capture and
projection of its actual vertices identify all three panes:

| Pass | Vertex programs | Captured PS | Canonical key | Blending |
| --- | --- | --- | --- | --- |
| Transmission/tint | 24, 47 | `75DAF346` | `05965D28` | Destination color / zero |
| Reflection | 34, 57 | `6E221C9E` | `2E7DB5E0` | One / inverse source alpha |

Vertex programs 24/34 draw static glass; the captured 47/57 pair covers model
glass. In frame 11312 the static panes occupy draws 395–400. The transmission
pass has no texture stages: its actual combiner blends two tint constants using
the vertex colors. Substituting the ordinary textured fallback can multiply
the existing framebuffer by black. The reflection pass then cannot restore
the scene that should be visible through the glass.

The two captured definitions and four observed program pairs produce three
fragment assets: tint, cube reflection and reflection with a 2D binding. They
are included in the lookup table and embedded in the executable. Existing
generated fragment sources are byte-for-byte unchanged.

Validation so far:

- The table regression fails on the old package and passes for all four vertex
  programs, including the alternate reflection binding.
- The native compiler emits all three GXP assets with zero failures.
- `tools/test_ps_programs.py` passes 4,376 C/Python identity checks and 2,014
  comparisons of generated texture variants. Draw preparation/cache tests pass.
- The Vita build passes. In the next Vita3K cryo-room run both glass
  programs link, all three observation windows show the room and technician
  behind them, and their tint/reflection remains visible. The cryopod windows also show their interiors after exiting the pod;
  flashlight-on glass remains transparent and the pod bodies remain visible.

Separate vertex-output-default and projected-texture experiments did not fix
these windows. They were removed from the test game's shader overrides before
validating this change and have not been applied to repository shader code.
Hardware glass and broader material coverage remain pending.
