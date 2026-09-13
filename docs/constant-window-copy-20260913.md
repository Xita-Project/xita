# Bulk shader-constant capture — September 13, 2026

Draw recording now captures a valid contiguous shader-constant window with one
`memcpy`, replacing a bounds check and a 16-byte copy for each register. Windows
outside Xbox registers `c[-96..95]` retain the same zero padding. The frame owns
the captured bytes; generation-based reuse and GPU retirement are unchanged.

This removes bookkeeping from the recording thread. It does not reduce the
number of constants, change shader arithmetic or introduce another worker. The
existing upload of constants once per frame remains in place.

`tools/test_constant_window.py` passes 18,913 byte-exact comparisons under
ASan/UBSan, including every valid start/end pair, signed base extremes, large
counts, empty windows, padding and unchanged source data. The existing
`tools/test_frame_constants.py` checks also pass, covering 12,800 windows and
the disabled upload-ring path. VitaSDK assembly shows one `memcpy` on the usual
valid-window path; these checks do not establish a hardware timing improvement.

The combined private build renders normal solo Blood Gulch startup, camera
movement, pause/leave and the campaign cryo room in Vita3K. Its 176 reporting
windows record no rejected constant draws. The object-basis experiment is also
enabled in this build, with 1,004,733 accepted calls and no FP/layout declines.
The run is a functional check, not an isolated performance comparison.

Tested EBOOT SHA-256:
`0c4377c9ab0564d53c57054f8d01d0ac3ca7080d09bcfecdad8bf242ce4d5b59`.
Private artifacts, screenshots and the stopped-run log are retained under
`audit/constant-window/` in the phase-followup directory. No Vita installation
was attempted. Hardware frame-time measurement remains pending storage recovery.
