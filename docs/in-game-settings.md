# In-game graphics

Press **Select + Circle** together to open Xita's graphics panel over Halo.
Use **Up/Down** or the left stick to select an option, **Left/Right** to change
it, and **Circle** to close. All fourteen dashboard graphics options are available
in the source candidate; older releases may have fewer options.

The panel consumes controller input while open. It does not pause the game;
press **Start** to pause Halo before opening it if needed. Closing the panel
returns control after the menu buttons are released.

| Applies during play | Saved for the next Xita launch |
| --- | --- |
| Render resolution | Texture detail |
| Texture filtering | Material quality |
| Mip smoothing | Glow / lens flares |
| Frame limit | Particle detail |
| Triple buffering | Decal lifetime and limit |
| | More compressed textures |
| | Model detail (source candidate) |
| | Indexed vertex checks (source candidate) |

The selected option explains when it takes effect. Values save to `xita.cfg`,
preserving unrelated settings and comments. A failed save leaves the previous
value in place. If a resolution allocation fails, rendering falls back to native
resolution and the panel reports it. Triple buffering remains experimental and
defaults off; it may increase input delay.

**Indexed vertex checks** is an experimental optimization that reduces repeated
CPU comparisons for geometry. It keeps the referenced vertex data and rendering
unchanged. It defaults to **Off** and requires relaunching Xita after editing.
Three physical cryo-room comparisons improved from about 6.16 to 6.74 FPS;
broader gameplay and additional views still need validation. See the
[measurements and limits](vertex-references-20260908.md#september-14-hardware-follow-up).

**Model detail** defaults to **Original**. **Balanced** and **Low** select Halo's
existing simpler character/scenery meshes sooner where the Xbox maps provide
them. Weapons and shared vehicle models retain their original settings. Many
rocks and trees have no alternate meshes, so this option does not reduce their
detail. Close and relaunch Xita after changing it. Hardware performance gains
remain unmeasured; see the [implementation and limits](model-preparation-20260909.md).

The panel is disabled while a benchmark is active. It draws at the Vita's native
960×544 resolution even when the game uses a smaller render target.

For developers: the recording thread owns the menu model and copies its view
into the existing frame slot. The render thread draws one extra batch in the
final scene; its normal completion fence protects the panel's vertex storage.
Only live edits drain published work. Resolution changes use the existing idle
pump handoff. No environment variables are rewritten while workers are running,
and unchanged frames add no settings-related waits or draw calls.

Validation: native build, host input and save-failure tests, frame ownership and
resize tests, and isolated Vita3K visual checks. Physical Vita testing of this
panel remains pending; it did not produce the September 8 hardware FPS results.
