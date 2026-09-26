# Suppressing identical vertex-constant uploads

`XV_VSC_EQUAL=1` is opt-in and requires `XV_REC_VSC=2`. In the existing finite,
single-guest-page fast path, compare the uploaded bytes with the live constant
rows before copying. An exact match preserves the existing dirty range instead
of extending it. Pending changes from earlier calls remain pending. Changed data,
non-finite data, page crossings and the original verify mode retain their paths.
The option is read once; restart Xita after changing it.

The deferred recorder currently copies the dirty bounding range into its queue
before its consumer compares constants. Suppressing an identical write earlier
can avoid enlarging that range and avoid redundant copies. It adds a comparison
for changed uploads, so a hardware benefit is not assumed.

`[vsc-equal]` reports eligible checks, skipped uploads, and bytes whose setter
copy was avoided. These bytes are not measured queue savings: other pending
updates may still cover the same rows. Cold worker mirrors and producer changes
still trigger the recorder's existing full synchronization.

Validation: `python3 tools/tests/test_vsc_equal.py` extracts the production setter
and `SetAllConstants`/`SetTrackedConstants` consumer. It compares baseline and
candidate guest return state, live rows, a delayed copied mirror, recorded rows,
and generation changes across 10,000 updates. Inputs cover clipped ranges,
non-finite values and page boundaries; consumers alternate sources and simulate
UI invalidation. Host ASan/UBSan and Pi ARM tests pass, with 516 identical uploads
skipped. This is not an end-to-end threaded recorder test or proof of Vita FPS.

Hardware acceptance still requires equal rendering and actual frame-time
improvement. No default or tester setting is changed by this candidate.
