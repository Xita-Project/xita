# Object sound properties: owner handoff

A retained physical run of runtime `3753961f…` ends with an explicit worker STOP
at `00194470`, returning to `00029898`, on lane 0. The target is
`CDirectSoundStream_SetFrequency`; the indirect chain is `C3A00 -> 2A180`.
The full 1,921,610-byte log was preserved as
`physical-object-voice-stop/later-previous-run.log` in private validation data.
This is a different unsupported sound call from the earlier voice-stop failure.

The audit covers `297B0` and its `296D0` helper. In addition to the already
supported volume call, their six stream-property calls now use the existing
quiescent guest-owner handoff:

| Handler | Target | Audited return | Arguments |
| --- | --- | --- | --- |
| Frequency | `194470` | `29898` | 2 |
| Maximum distance | `193D9B` | `298E7` | 3 |
| Minimum distance | `193DB3` | `2992A` | 3 |
| Cone angles | `193D68` | `2999D` | 4 |
| Cone outside volume | `193D96` | `299F0` | 3 |
| I3DL2 source | `193E22` | `2979A` | 3 |

Only those return sites are admitted. All object lanes park before the real HLE
handler runs on the guest owner. Frequency preserves stream lookup, reporting
schedule retiming and the mixer's existing frequency setter. The other five
handlers currently acknowledge spatial settings through existing DS_OK stubs;
this handoff preserves those semantics and does not implement new spatial audio.
None dispatches a guest callback. Unsupported nested calls remain rejected.
Frequency and spatial-property counters report executed owner services separately.

The production suite passes with two, one and zero workers, profiling off/on and
bounded waits off/on, under normal compilation, ASan/UBSan and ThreadSanitizer.
Each run executes 600 frequency updates and 3,000 spatial-property calls, inside
and outside held shared transactions. Tests compile the real handlers and check
owner-thread execution, parked publication, known/unknown streams, frequency
boundaries, mixer arguments, reporting deadlines, retained packet data, registers,
argument counts, stack cleanup and counter reset. Every property rejects an
unrelated return site.

Native compilation and package verification pass. Runtime SHA-256:
`37f761dc071a7489f8dcd176bcbbe6e4152d4a0f03d3a9dbff2e13e842c28627`.
The build also contains the opt-in replacement-blend comparison, disabled by
default. Vita3K loads Blood Gulch through the normal menus and completes a
charged plasma shot. The physical updater confirms the full hash above running
in slot B, with the preserved slot A unchanged. Both object workers remain
enabled; replacement blending remains off. Longer gameplay must verify the
newly admitted parameter calls. No FPS gain or overall stability claim is made
for these audio handoffs.
