/* Retained scalar draw-state import, before native batching. */
static uint32_t reference_gl_blend_to_d3d(uint32_t gl)
{
    switch (gl) { case 0: return 1; case 1: return 2; case 0x300: return 3; case 0x301: return 4; case 0x302: return 5; case 0x303: return 6;
                  case 0x304: return 7; case 0x305: return 8; case 0x306: return 9; case 0x307: return 10; case 0x308: return 11; default: return 2; }
}
static void reference_sync_draw_state(void)
{
    uint64_t profile = xv_draw_profile_begin();
    xd3d_ps_sync();
    xv_d3d_SetAllAttributes(xd3d_current_attributes());
    xv_d3d_SetPixelShader(xd3d_state.ps_hash, xd3d_state.ps_key, xd3d_state.psc);
    for (unsigned i = 0; i < 4; ++i) {
        xv_d3d_SetStreamSource(i, xd3d_state.stream_vb[i], xd3d_state.stream_stride[i]);
        xv_d3d_SetTexture(i, xd3d_state.texture[i]);
        xv_d3d_SetTexturePalette(i, xd3d_state.palette[i]);
        /* XDK 3925 orders ADDRESSU/V at 10/11 and MAG/MINFILTER at 13/14.
         * The deferred setter already updates this guest table. Leaving GXM's
         * mesh defaults here forced point sampling even for filtered lightmaps. */
        xv_d3d_SetTextureStageState(i, X_D3DTSS_ADDRESSU, xd3d_texture_state(i, 10));
        xv_d3d_SetTextureStageState(i, X_D3DTSS_ADDRESSV, xd3d_texture_state(i, 11));
        xv_d3d_SetTextureStageState(i, X_D3DTSS_BORDERCOLOR, xd3d_texture_state(i, 29));
        xv_d3d_SetTextureStageState(i, X_D3DTSS_MAGFILTER, xd3d_texture_state(i, 13));
        xv_d3d_SetTextureStageState(i, X_D3DTSS_MINFILTER, xd3d_texture_state(i, 14));
    }
    /* the kernel model keeps the NV2A/GL tokens the game wrote; xv_d3d speaks D3D enums */
    xv_d3d_SetStencil(&xd3d_state.stencil);
    xv_d3d_SetRenderState_ZEnable(xd3d_state.z_enable);
    xv_d3d_SetRenderState_ZWriteEnable(xd3d_state.z_write);
    xv_d3d_SetRenderState_ZFunc(xd3d_state.z_func >= 0x200 && xd3d_state.z_func <= 0x207 ? xd3d_state.z_func - 0x200 + 1 : 4);
    xv_d3d_SetRenderState_CullMode(xd3d_state.cull == 0x900 ? 2 : xd3d_state.cull == 0x901 ? 3 : 1);   /* GL_CW / GL_CCW / none */
    xv_d3d_SetRenderState_AlphaBlendEnable(xd3d_state.alpha_blend);
    xv_d3d_SetRenderState_BlendOp(xd3d_state.blend_op);
    { uint32_t m = xd3d_state.color_mask; xv_d3d_SetRenderState_ColorWriteEnable(((m >> 16) & 1) | ((m >> 7) & 2) | ((m & 1) << 2) | ((m >> 21) & 8)); }   /* -> D3D bits R1 G2 B4 A8 */
    xv_d3d_SetRenderState_SrcBlend(reference_gl_blend_to_d3d(xd3d_state.src_blend));
    xv_d3d_SetRenderState_DestBlend(reference_gl_blend_to_d3d(xd3d_state.dst_blend));
    xv_draw_profile_step(XV_DRAW_STATE, &profile);
}
