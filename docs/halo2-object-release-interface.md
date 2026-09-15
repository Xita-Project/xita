# Original rigid-body release interface

Native217 reaches `311570` through `1D0321`, return `1D0323`. The unchanged caller decrements the original reference count, checks the original protected arena conditions, then passes deleting flag1 to vtable slot0. The native EAX value is table `414310`. That table follows the owned executable's `hkRigidBody` name, contains five original callbacks and ends at the null at `414324`; the neighboring interface beginning `414328` is excluded.

Constructor `311BC8` and destructor `31142E` assign the same table. Original `311570` calls destructor `311410`, conditionally invokes the allocator with the object's stored size and allocation category, then returns with `ret 4`. Discovery admits the five verified `.text` targets; no host shortcut changes reference counts, frees objects or supplies cleanup results. Original nested callbacks and allocator checks continue normally and unsupported behavior remains strict.

The constructor binding, destructor binding, observed dispatch and full table including its null boundary are independently fingerprinted. All 58 callback-root tests pass, including every altered callback, each fingerprint, invalid boundary and wrong code section. The synthetic fixture omits the following interface and preceding text, so a wider scan fails. Private evidence is `object-release218/`; owned bytes, generated code, traces, captures and packages remain outside Git. Diagnostic packages embed owned material and must not be uploaded as distributable releases. No Halo CE, hardware or shared emulator changes are included.

Generation adds ten reachable functions, totaling 13,573 functions, 177,807 blocks and 1,240,823 instructions. All prior 13,563 function bodies remain identical apart from surrounding separator whitespace. The unsupported count stays 3,663, with 106 referenced kernel exports. These metrics describe generated coverage, not compatibility.

The native build changes the automatic chunk size and emits 127 generated code units. The copied prior `code_127.o` is absent from the actual linker command; the post-build dependency gate detects its now-missing source before any launch. Its object and dependency file are preserved outside the build, and the build script now retires obsolete units before invoking Make. The completed checks verify all 179 live dependency targets, 129 C source paths and 178 objects. Only 128 generated-code/function-table objects differ; all 50 runtime/support objects remain byte-identical.

Native218 executes the original rigid-body destructor, including both original motion-type queries, then stops at nested deleting destructor `31A710` from `31148A` in `311410`. Native EAX is `4151B0`, ECX `014F05C0`, ESP `005E1DE0`. The original Microsoft Game Studios intro is visually verified in `native-218-view/early-movie-middle.png`. Last frame136 and channel remain unchanged and RGB-zero; no original main menu appears. Owned PID `3770310` was stopped after capture.

| Native218 artifact | SHA-256 |
| --- | --- |
| `halo2-boot.elf` | `b1de9cac7c4c8f449b2bfd4130117dbcb67a1fdc6872586332eff2ad78327422` |
| `eboot.bin` | `cf5799a61741c44e5040f02705b5cba44f6e5f581fb6fc9b32897240d5469c39` |
| `boot.log` | `895a6721a2efe19b30c304a9e31a7133eb2abbb066f2a79871f184905cfc086f` |
| `channel-at-stop.json` | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| `last-presented-at-stop.bin` | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

Exact frozen replay from the private directory uses `python3 preserve_fresh_cache.py replay218`, `python3 capture_run.py replay218 native-218-artifacts`, then `python3 drive_startup.py replay218 native-218-artifacts`, sequentially with successful exit checks and no other owned main-lab process. The next task is admitting the verified nested motion type/release slots while preserving original cleanup and allocator behavior.
