# Halo 2 menu startup requires sound initialization

Native87 loads the original mainmenu map, executes the newly discovered map and
descriptor initializers, and then stops at a null sound-object call. This confirms
a limit of the explicit `--audio-unavailable` diagnostic. Returning the genuine
no-driver error permits earlier startup but does not provide a supported audio-free
route through this map initialization. There is no visible movie or menu.

## Causal evidence

Only bounded read-only probes were added. They log at the original cleanup,
map sound setup and object invocation entries, read stack and object words only
through validated guest mappings, and preserve native FPSCR. They supply no sound
object, alter no guest memory or control flow, and leave the terminal call intact.

The single native87 run establishes this chain:

1. Original `21E4B0` zeroes its sound-state allocation and calls public
   `DirectSoundCreate` at `21E516`. The diagnostic returns `88780078`, output
   untouched. At cleanup entry `21EAE0`, EAX is that HRESULT; state `007317BC`
   has initialization word zero, device slot `0073426C` zero and selected-object
   slot `00734270` zero.
2. The original `21E51D` failure branch skips the remaining initializer,
   including the assignment of original object `47F088` to state+2AB4 at
   `21E8EE`. The original caller checks the false initialization result and
   continues the enclosing startup, as documented in the earlier error probe.
3. After real mainmenu loading, original map callback `125DF0` calls `21F6D0`
   (return `125E33`) using map data at `801DF104`. The same state allocation
   still has both object slots zero. The observed caller stack also contains
   `12576E` and the map lifecycle dispatch return `137CDC`.
4. `21F70A` loads ESI from state+2AB4, hence ESI=0 at entry `21E3B0`, return
   `21F715`. The original virtual call at `21E3E8` then traps at target zero,
   with return `21E3EA`. Adding a code-discovery root cannot repair this state.

The original successful-path object `47F088` has vtable `457140`; the three
observed slots are `2AEB20`, `175F40` and `2AEF50`. Writing that object into the
failed initializer's state would bypass required setup, not implement a backend.
No such write is made. The original no-driver baseline and strict APU access
stop remain available and unchanged.

## Next implementation boundary

A real XDK5849 DirectSound adapter is required before pursuing later menu draws.
The existing software mixer in `recomp/kernel/xk_audio.c` and Vita audio sink may
provide reusable primitives, but the CE DirectSound object wrapper must not be
substituted without an independent ABI and lifetime audit. In particular, the
owned public wrapper returns internal-object+8 on success; subsequent wrappers
subtract eight before calling internal methods.

The first successful-path calls after creation are already bounded privately:

| Public entry | Stack contract | Observed internal behavior |
| --- | --- | --- |
| `37B5AE` | object, output; RET8 | Writes four DWORDs through output |
| `37D506` | object, scalar bits, apply flags; RET12 | Stores listener+68, marks bit8, commits unless deferred |
| `37D5CD` | object, scalar bits, apply flags; RET12 | Stores listener+6C, marks bit10, commits unless deferred |
| `37B5CA` | object, output; RET8 | Writes a configuration DWORD with bit31 cleared |

These are binary ABI observations, not complete API implementations or claims
that those four calls are sufficient. Subsequent creation, mix-bin, buffer,
stream and effect operations still require audits and actual semantics. The
[primary Cxbx DirectSound implementation](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/DSOUND/DirectSound/DirectSound.cpp)
illustrates a host device plus separate method adapters; its unfinished methods
are not evidence that success-only replacements are correct.

The next bounded task is to identify these methods against primary API definitions,
implement checked device ownership and real host output, and validate the first
creation/query/setter sequence with synthetic ABI/lifetime tests. Unsupported
subsequent operations must remain explicit stops. A dummy object or skipped call
would conceal this dependency and is not an acceptable menu milestone.

## Native validation and reproduction

The private native87 build passed with four jobs. Generated source is unchanged
from the 24-test descriptor-map milestone. The new probes are diagnostic logging
only; this does not introduce another public API or replacement to host-test.
Both native86 and native87 reach the identical terminal address, channel JSON
and final scanout. Native87 copies all 59,670,016 bytes of mainmenu into its private
cache; GET=PUT=`03C2ADE4`, with no remaining packet. Frame557 has all 522,240 pixels
`FF000000`. No title/dashboard launch probe fires. An attempted Start input was
sent after the terminal trace had already closed, so no transition is attributed
to that input: native87 repeats the automatic movie-end result of native86.

Private evidence is `../private/native-87-artifacts`, `native-87-view`,
`native-milestone-87.json` and `native87-audio-audit`. The latter contains original
caller disassembly and exact wrapper ranges/hashes. Frozen generated source is
`../private/descriptor-map-lifecycle/generated`.

| Artifact | SHA-256 |
| --- | --- |
| ELF | `d4eae26362455400b95ba634aef4edb9494cc5f3fc6e3c91f8aaf261207147fe` |
| EBOOT | `fdad4148f70d5f800f5c7df32fb1728ea02cf72a8e271a82ffc187188ba0627a` |
| VPK | `ad6114abe663016eae23ddce4335cbb3154045d19b010a6e07e3d4197374434f` |
| Trace | `0bacbee400cf943b87b2dcd645b6027235be8c8e1916d59f163228924bca3e4f` |
| Channel JSON | `7b4cfd10f90539a1dc2c8989c32e1095593658e105ed88d3f35c4a9c2d548b39` |
| Frame | `e9a77359c987d6322d057c67a83a6367f7e8e61ed16cb35e76a89b8c56bee49d` |

Stop only the private Halo 2 emulator. Preserve the current private cache4
intact by renaming it to an unused private path, then create an empty cache4
folder. From `../private`, replay with:

```sh
python3 run_lab.py REPLAY_NAME native-87-artifacts/halo2-boot.vpk
```

Allow the original formatter, map copy and movie timer to finish. This retains
the [populated-volume raw-access limitation](halo2-exit-cache-diagnostics.md).
No owned maps are modified. The emulator is stopped after the archived run;
no Halo CE process or hardware was touched. Owned code, data, generated source
and diagnostic packages remain private. These game-embedded packages must not
be uploaded as distributable releases.
