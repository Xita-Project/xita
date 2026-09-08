# Visibility queries — September 6, 2026

Halo's flare intensity depends on the number of visible pixels in a test
rectangle. The port returned one pixel for every query, while Begin/End did
nothing. After restoring immediate VS56 flare draws, this still left the plasma
charge glow nearly invisible.

The D3D bridge now records query membership with each draw. The render pump uses
GXM visibility counters and publishes results after its existing frame Finish,
before releasing the recorded frame. There is no extra per-query Finish. The
captured Halo call order reads the preceding frame's results before issuing new
queries; incomplete results return the original Xbox HRESULT `88760828` and leave
output memory unchanged. The original executable's API was checked directly.

Each of the two frames owns four GPU counter arrays, one per SGX543MP4 core.
512 query slots cover Halo's observed 384-entry allocation limit. The arrays
use 16 KiB total of uncached mapped memory. CPU result generations prevent a
previous completion from satisfying a newer query with the same ID. Visibility
state changes are cached, and clear/UI/upscale draws run with testing disabled.
Counters are converted from the actual render resolution to the recorded guest
viewport area; Halo divides the count by that guest rectangle area when computing
flare opacity. Both 848x480 and 960x544 scaling are covered by tests.

Validation includes the actual HLE and result publication code: pending/success
and error returns, stack cleanup, output writes across noncontiguous guest pages,
four-core count summation, zero coverage, saturation, repeated IDs, exhaustion,
GXM enable state, and 100,000 asynchronous handoffs. Host and ASan/UBSan runs pass.
The frame completion test verifies that publication occurs after GPU completion
and before frame reuse. Existing shader, draw-preparation, immediate-flare and
render-target lifecycle checks and the native Vita build pass.

Vita3K now displays the plasma pistol's idle glows and a bright charged orb.
The query log distinguishes visible weapon glows from a fully hidden query;
resolution-normalized counts are recorded with `XV_LOG_VISIBILITY=1`. Normal solo
split-screen launch continues to work. Hardware appearance, GPU query cost,
other flare sources and hardware performance remain unverified. No Vita USB
storage was connected during this validation.

The optional Xbox timestamp retains the null renderer's zero; Halo's inspected
helper does not use it. This implementation targets Halo's frame-delayed usage;
a game that waits on a newly issued query before submitting its frame would need
a partial command-list submission path. Allocation/API failure reports zero
coverage so result polling still completes.
