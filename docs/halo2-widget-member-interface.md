# Halo 2 original incoming-widget member

Native 177 observes `2540D3` from `22E65E`, return `22E6C9`, object
`825384A0`, vtable `45A628`, ESP `005E5ED4`. The object is exactly the
incoming widget `82537E90` plus `610h`. Constructor `22F15D` initializes
that member through `253C8B`, which installs `45A628` and the original
member state. This is a proven containment link, not an inferred type from
a shared method address.

The observed `18h` getter calls original lookup `253CC8`. That lookup finds
the containing widget, obtains its descriptor list and checks that the signed
member selector at `F8h` is nonnegative and within the list count before
selecting a 3Ch-byte record. The getter returns the record's signed word at
`06h`, or the original zero result when no descriptor is available. Both
returns, including the branch after the first `ret`, remain in guest code.

Discovery adds the exact 18-method interface ending at `45A670`; a full
72-byte fingerprint covers its entries. The following data is not scanned.
This retains original update, draw, event and lifetime methods. The `44h`
method's complete body is also checked; its resource lookup and conditional
result remain unchanged. No descriptor, animation or input state is invented.

Seven full fingerprints cover the parent constructor, member constructor,
observed caller, lookup/getter, `44h` method and exact interface, alongside
the existing whole-image gate. Every included target must be executable
title `.text`. All 67 focused callback, profile, LOOP and sparse-jump tests
pass, including every guard, invalid targets, missing section metadata and
untouched adjacent data. No shared runtime, graphics or sound changes.

Private proof: `widget-setup/member-interface-guards.json`,
`member-interface-full-bodies.txt`, `member-interface-references.json`.
Regeneration adds nine function entries, 55 blocks and 384 instructions;
unsupported instructions remain 3,792, with 4,645 inferred switch targets.
These are translation metrics, not executed coverage. Native 178 uses
`widget-member`.

Native 178 passes member setup and reaches unsupported scalar SSE `CMPSS`
at `2CE15` in original function `2CBF0`. The local sequence performs
`CMPNEQSS`, `CMPLTSS`, `ANDPS`, then currently unsupported `MOVMSKPS` at
`2CE26`; it adjusts an integer converted from the original scalar value.
No instruction is bypassed. The terminal registers are EAX `1A9`, ECX `1F`,
ESP `005E5870`. The next task is exact comparison/sign-mask support, with
independent instruction and exception-state tests.

The original Microsoft Game Studios intro is visibly confirmed. Normal Start
follows the full 59,670,016-byte map copy; the transition recording contains
55.988858 seconds. No main menu appears. Final frame 135 remains black and
the decoded channel snapshot is unchanged. The raw push-buffer snapshot has
changed; that is not evidence of a newly consumed draw or rendered frame.

All 171 native dependency targets pass verification. Frozen artifacts and
hashes are in `native-178-artifacts` and `native-milestone-178.json`:

- ELF: `e60aebf71ccfe581fd80545711ca75686bd544f03cc1ab573f7985b17f1e4073`
- EBOOT: `817a20b9d84884ca3cd19bc8c9e3018926779edae70f3cf6ed8adb547c052ead`
- Guest trace: `c32daff67abb8b60770f30de3e6aff9c91e1316356e72a126bab458bb3f2a164`
- Raw push buffer: `93e5163ec5d8f54edcde2e5c0f60b6e695026bb907108ddd60866bc4aec88ec6`

From the private directory, replay with
`python3 preserve_fresh_cache.py native178-replay`,
`python3 capture_run.py 178-replay native-178-artifacts`, then
`python3 drive_startup.py 178-replay native-178-artifacts`.

The diagnostic package embeds owned game content and must not be distributed.
