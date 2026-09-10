# September 9 USB model/material candidate update

The Vita was running the untraced September 9 gameplay executable: installed
SHA-256 `5bd4663efe5e3a7a93d8e3923888812180e946458169cf9840942b42546e611f`.
The newest collected log confirms sampling is off and guest function tracing is
absent. It includes resolution changes during play, so the run is not a fixed
graphics-setting comparison.

After backing up the executable, logs, configuration and recent screenshots,
the [model/material preparation candidate](model-preparation-20260909.md) from
source `22331f4` was installed through USB executable replacement. All 1,579
packaged shader and `sce_sys` files were verified unchanged against the candidate.
The new executable was staged, flushed and verified before replacement, then
read back and checked. USB was safely unmounted. No VPK installation is required
for this update.

Installed `eboot.bin`: 31,099,214 bytes, SHA-256
`3e91e7f9d54adbe096a8e42e239018cf50ff14fd404a114c277265866203f573`.
The matching local VPK is 13,297,326 bytes, SHA-256
`77c028d6650527e731909e25274a66ca3cdf340ef0748cad0d0cc8d7c0f4ffe3`;
it has not replaced the published gameplay release.

The configuration was preserved byte for byte. Model detail defaults to
**Original**. Test the automatic shader logging/color-preparation changes with
the usual settings first. Compare **Low** after closing and relaunching Xita,
using the same campaign encounter; most Blood Gulch scenery has no alternate
LODs. Physical FPS and stability of this candidate are still unmeasured.

## LiveArea and installation follow-up

The user still reports a blank/plain LiveArea background. The app contains the
revision-3 template and matching PNG assets, so those files were installed.
The template structure agrees with the [VitaSDK hello-world sample](https://github.com/vitasdk/samples/blob/master/hello_world/sce_sys/livearea/contents/template.xml).
This update does not claim to repair the wallpaper.

The USB-visible `appmeta` copies are protected storage rather than plain XML/PNG;
VitaShell's [refresh implementation](https://github.com/TheOfficialFloW/VitaShell/blob/master/refresh.c)
mounts that metadata through PFS before reading it. Raw USB bytes cannot establish
whether its decrypted cache matches the app assets. Do not replace these protected
files with plaintext or label their raw encoding corruption. Inspecting the
decrypted metadata on the Vita is the next diagnostic step.

The published gameplay VPK passes CRC validation but contains 1,574 separate
shader files. Small-file installation overhead is a plausible explanation for
the reported 1 KB/s near 98–99%, not a proven cause. A future validated shader
bundle can reduce the file count; it requires a matching runtime loader.
