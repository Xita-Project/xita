# Additional captured effects programs — September 6, 2026

Four a10 programs had no canonical entry in the fragment table. The captured
definitions and observed vertex-program pairs now produce four additional
embedded fragment assets. Existing generated fragment sources are unchanged.

| Vertex program | Captured PS | Canonical key | Observed operation |
| --- | --- | --- | --- |
| 01 | `8F1C9CBE` | `A946F66C` | Destination-color texture/constant blend |
| 01 | `862D7A3E` | `660E94EC` | Related half-bias blend |
| 38 | `8C25D9E4` | `D24A1DC6` | Four-texture average/blur |
| 47 | `299E6005` | `875E3685` | Four-texture additive lighting pass |

The native compiler emits all four assets with zero failures. The previous
table fails the captured-pair regression; the new table, draw preparation/cache
tests, shader identity/source comparisons, and AddressSanitizer/UBSan checks
pass. A native build starts Blood Gulch through the normal solo menu and
preserves plasma charge/discharge behavior.

This adds missing program coverage; it does not establish a fix for all bloom
or smoke effects. The captured a10 passes need further visual checks, and
hardware validation is pending. The radar's separately reproduced black spot
requires its [zero-alpha blip program](radar-blip-20260906.md).
