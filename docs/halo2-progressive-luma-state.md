# Halo 2 digital progressive soft-filter state

Native65 requests Boolean luma/soft filtering after applying the sole supported
progressive mode. The adapter now retains this preference after mode application.
It still rejects enable before mode application, values above one, and output
pointers. This is a digital RGB presenter; no analog reconstruction or SD luma
bandwidth filter is simulated, and the preference causes no framebuffer rewrite
or presentation call.

The primary Conexant data sheet 100381B, section 1.3.45 (page 1-85), describes
the VGA/DAC route through the encoder; section E.9 (page E-15) requires that route
for HDTV output. Section E.6 (page E-12) separately documents bypassed flicker
and FIFO blocks in HDTV mode. This supports the limited digital output contract,
not an assertion of analog signal equivalence or an implementation for interlaced
modes or other encoders. [Manufacturer data sheet, mirrored PDF](https://datasheet.octopart.com/CX25871-15-Conexant-datasheet-180221357.pdf),
SHA-256 `be7e918c84bf7bdabe900fe61ec53917e1347e359b15b4b2a2b9fc9330d04a79`.

All 18 host executables plus the timed case pass. Both runtime cases pass
ASan/UBSan. Tests cover exact retained Boolean state, repeats and disable,
unchanged CPU state except the return stack, no presentation/blanking calls,
and rejection without state/output mutation.

Native66 completes the request and enters the second original main-loop
iteration. It strictly stops on software swap method `0100`, word `74280021`,
GET `03B4432C`, PUT `03B44370`, at guest return `3FAC58`. The pending
active-scanout timed flip has not executed. The original error screen remains
visible; there is no main menu or observed mainmenu.map loading.

Read-only mapped state capture shows `[4E6948]=82D486E0`, its first word
`FFFF0000`, and byte at `+1120` zero. The original mode object at `544B20`
has zero words at `+8` and `+18`. Thus the original active-map predicate rejects
and uses the fallback rendering branch. The cause of the error bitmap selection
remains under investigation; no game flag, file result, or original branch was
changed to advance startup.

| Private native66 artifact | SHA-256 |
| --- | --- |
| ELF | `1bf070d9860b3f229b767edaad86a8d05b369dc084a6ba3d1754b347b2f6457a` |
| EBOOT | `59f67496ab299c58a18ce4cd501fa17892b19e32e65e93e78f0f1b1a0955ec05` |
| VPK | `e349a6b921ddd0e88220e904c75a6a597b3d2b93a6777c70cb7842ca9bb2ed65` |
| Boot trace | `f19f1b48e12a408da949db4d7c3df96288fe62dfdb5a3da88ec856f5cabaf25b` |
| Channel state | `7e1d211e1cbe203f56a228e7e41bc6a9e95b2473d477c0efee845116c23f0f7f` |
| Last presented buffer | `a7d8be6fbe31bcc71b47ffc9d958471253719e82a3f032d29bf5af3727260abf` |

Replay from source: `python3 ../private/run_lab.py replay66 ../private/native-66-artifacts/halo2-boot.vpk`.
Screenshot: `private/native-66-view/window-4.0.png` relative to the worktree parent.
The generated source remains `online-interfaces/generated`. The diagnostic
package uses the explicit unavailable-audio and limited native FP probes,
embeds owned game image/code, and must not be distributed or uploaded as a
release. All captures and game-derived artifacts remain private.
