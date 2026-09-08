/* Actual link cache with mocked shader creation; no GPU required. */
#include <assert.h>
#include "../../xv_d3d.c"
/* These tests isolate shader/state policy; owned vertex storage is exercised
 * separately against the production allocator and delayed completion. */
const void *xv_vertex_upload(unsigned slot, const void *p, unsigned n) { return p; }
void xv_vertex_upload_reset(unsigned slot) {}
void xv_vertex_upload_shutdown(void) {}
void xv_gpu_flush_pump(const void *p, uint32_t n) {}

static unsigned loads, unloads;
static int fail_load, fail_no_alpha, fail_gt;
static char loaded_path[160];
void xv_logf(const char *fmt, ...) { (void)fmt; }
int xv_fshader_load(xv_fshader_t *fs, const char *path, const xv_vshader_t *vs,
                    const SceGxmBlendInfo *blend)
{
    (void)path; (void)vs; (void)blend;
    loads++;
    snprintf(loaded_path, sizeof loaded_path, "%s", path);
    memset(fs, 0, sizeof *fs);
    if (fail_load || (fail_no_alpha && strstr(path,"_na.frag.gxp")) || (fail_gt && strstr(path,"_gt.frag.gxp"))) return -1;
    fs->prog = (const SceGxmProgram *)(uintptr_t)loads;
    return 0;
}
void xv_fshader_unload(xv_fshader_t *fs)
{
    if (fs->prog) unloads++;
    memset(fs, 0, sizeof *fs);
}

int main(void)
{
    /* Low material detail selects the basic model VS. Its combiner must use
     * that VS's reduced varying set rather than the texture-color fallback. */
    assert(ps_entry_for(0x109DB448u,0x1955B8E9u,0)>=0);
    assert(ps_entry_for(0x109DB448u,0x1955B8E9u,8)>=0);
    /* Actual a10 glass draws: a texture fallback on the destination-color
     * transmission pass turns the window black. Both BSP and model variants
     * need their captured combiner, including the 2D reflection binding. */
    const uint32_t glass_transmission[] = {0x9e6f8b13u, 0xac1984dbu};
    const uint32_t glass_reflection[] = {0x20df9066u, 0xff7e5030u};
    for (unsigned i=0;i<2;i++) {
        assert(ps_entry_for(glass_transmission[i],0x05965d28u,0)>=0);
        assert(ps_entry_for(glass_reflection[i],0x2e7db5e0u,0)>=0);
        assert(ps_entry_for(glass_reflection[i],0x2e7db5e0u,1)>=0);
    }
    /* Captured effects must keep their combiners: destination-color passes
     * and a four-input blur cannot use the ordinary one-texture fallback. */
    assert(ps_entry_for(0xf8a4ae90u,0xa946f66cu,0)>=0);
    assert(ps_entry_for(0xf8a4ae90u,0x660e94ecu,0)>=0);
    assert(ps_entry_for(0xbb2f446bu,0xd24a1dc6u,0)>=0);
    assert(ps_entry_for(0xac1984dbu,0x875e3685u,0)>=0);
    assert(ps_entry_for(0x4469e1f8u,0x44d896d9u,0)>=0); /* zero-alpha radar blip */
    assert(ps_entry_for(0x1daf0284u,0x4f2bb5b9u,0)>=0); /* two-texture scope mask */
    /* Fresh campaign/weapon captures: visibility alpha, colored flares,
     * two-input alpha composite, four-input blur, and reflected model light. */
    const uint32_t flare_keys[] = {0xf648d0a1u,0x19ab9bbbu,0x037c4aabu};
    for (unsigned i=0;i<3;i++) assert(ps_entry_for(0x405809d3u,flare_keys[i],0)>=0);
    assert(ps_entry_for(0xbb2f446bu,0x51622122u,0)>=0);
    assert(ps_entry_for(0xbb2f446bu,0x63c01584u,0)>=0);
    assert(ps_entry_for(0x9e2e9021u,0xe519bff9u,0)>=0);
    assert(ps_entry_for(0x9e2e9021u,0xe519bff9u,8)>=0);
    /* Snipers grenade capture: the dynamic model-lighting vertex program
     * needs this eight-stage combiner with both cube and 2D reflections. */
    assert(ps_entry_for(0x393556f8u,0xe9632f91u,0)>=0);
    assert(ps_entry_for(0x393556f8u,0xe9632f91u,8)>=0);
    /* The resumed a10 combat corridor uses inverse-alpha and additive
     * particle combiners, which cannot use the plain texture fallback. */
    const uint32_t corridor_keys[] = {0x5f39f5beu,0xd9a5be50u,0xe4cf977cu};
    for (unsigned i=0;i<3;i++) assert(ps_entry_for(0xf8a4ae90u,corridor_keys[i],0)>=0);
    assert(ps_entry_for(0x393556f8u,0xfcc09c58u,0)>=0);
    assert(ps_entry_for(0x393556f8u,0xfcc09c58u,8)>=0);
    /* Fresh a10 route and Keyes cinematic: retain the captured multi-texture
     * glass, model-lighting and screen programs through the BSP handoff. */
    const uint32_t bridge_pairs[][2] = {
        {0x166818f8u,0x681c8374u}, {0x20df9066u,0xa338fa66u},
        {0x71412768u,0x957471ecu}, {0x909ed3c4u,0x83399157u},
        {0x909ed3c4u,0x48170fd0u}, {0x9e6f8b13u,0x1a7d57fau},
        {0xac1984dbu,0x3d679075u}, {0xac1984dbu,0xd5b5783du},
        {0xac1984dbu,0xad86e3ecu}, {0xac1984dbu,0x79f47070u},
        {0xac1984dbu,0x58777bedu}, {0xac1984dbu,0xa7e0cdabu},
        {0xac1984dbu,0x4c56a7f9u}, {0xac1984dbu,0xfa814e66u},
        {0xdb4e520au,0x3167616bu}, {0xff7e5030u,0x62db64a6u}
    };
    for (unsigned i=0;i<sizeof bridge_pairs/sizeof bridge_pairs[0];i++)
        assert(ps_entry_for(bridge_pairs[i][0],bridge_pairs[i][1],0)>=0);
    assert(ps_entry_for(0x20df9066u,0xa338fa66u,1)>=0);
    assert(ps_entry_for(0xff7e5030u,0x62db64a6u,1)>=0);
    /* Later draw setters and the next recording frame must not change GPU
     * attributes captured for a decal that is still waiting to execute. */
    g_im_vertices = calloc(XV_NUM_LISTS, XV_IM_BYTES); assert(g_im_vertices);
    cmd_t first = {0}, second = {0}, next = {0}, full = {0};
    xv_d3d_SetVertexData4f(9, .1f, .2f, .3f, .25f);
    xv_d3d_SetVertexData4f(3, 1, 0, 0, 1);
    assert(snapshot_attributes(&first));
    xv_d3d_SetVertexData4f(9, 0, 0, 0, .75f);
    assert(snapshot_attributes(&second));
    g_build_frame++;
    xv_d3d_SetVertexData4f(9, 0, 0, 0, 1);
    assert(snapshot_attributes(&next));
    assert(((const float (*)[4])first.constant_stream)[9][3] == .25f);
    assert(((const float (*)[4])first.constant_stream)[3][0] == 1);
    assert(((const float (*)[4])second.constant_stream)[9][3] == .75f);
    assert(((const float (*)[4])next.constant_stream)[9][3] == 1);
    g_im_used[1] = XV_IM_BYTES - sizeof S.const_attr + 1;
    assert(!snapshot_attributes(&full) && !full.constant_stream);
    /* The complete opening cinematic rejected up to 960 more four-vertex
     * flares after using 65,520 bytes. Retain that workload while frame 0's
     * attribute snapshot is still in flight; caller scratch changes each draw. */
    g_im_used[1]=65520; g_im_requested[1]=65520;
    uint32_t flare[40]; const uint32_t *retained[960];
    for (unsigned i=0;i<960;i++) {
        for (unsigned j=0;j<40;j++) flare[j]=i*40+j;
        retained[i]=retain_immediate_bytes(flare,sizeof flare); assert(retained[i]);
        assert(!((uintptr_t)retained[i]&15u));
    }
    memset(flare,0,sizeof flare);
    for (unsigned i=0;i<960;i++) for (unsigned j=0;j<40;j++) assert(retained[i][j]==i*40+j);
    assert(g_im_used[1]==219120 && g_im_requested[1]==219120);
    assert(((const float (*)[4])first.constant_stream)[9][3]==.25f);
    unsigned used=g_im_used[1]; uint64_t request=(uint64_t)XV_IM_BYTES-used+1;
    assert(!retain_immediate_bytes(flare,request));
    assert(g_im_used[1]==used && g_im_requested[1]==used+((request+15)&~UINT64_C(15)));
    free(g_im_vertices); g_im_vertices = NULL;
    g_build_frame = 0; memset(g_im_used, 0, sizeof g_im_used); memset(g_im_requested,0,sizeof g_im_requested);
    uint8_t vertices[64] = {0}; uint16_t indices[] = {0, 1, 2};
    cmd_t geometry = {0}; geometry.streams[0] = vertices; geometry.indices = indices;
    geometry.geometry_bytes[0] = sizeof vertices;
    geometry.geometry_hash[0] = geometry_hash(vertices, sizeof vertices);
    geometry.geometry_bytes[XV_MAX_STREAMS] = sizeof indices;
    geometry.geometry_hash[XV_MAX_STREAMS] = geometry_hash(indices, sizeof indices);
    assert(changed_geometry(&geometry) == 0);
    vertices[37] = 1; assert(changed_geometry(&geometry) == 1);
    indices[2] = 0; assert(changed_geometry(&geometry) == (1u | (1u << XV_MAX_STREAMS)));
    /* Reproduce Halo overwriting its visible triangle list while the previous
     * frame is waiting for rendering. Both recorded frames keep their topology. */
    g_frame_indices = calloc(XV_NUM_LISTS * XV_FRAME_INDICES, sizeof(uint16_t));
    g_quad_indices = calloc(XV_NUM_LISTS * XV_QUAD_INDICES, sizeof(uint16_t));
    assert(g_frame_indices && g_quad_indices);
    indices[2] = 2; const void *saved_indices = indices;
    unsigned nverts = 0;
    assert(retain_indices(&saved_indices, 3, &nverts) && nverts == 3);
    indices[0] = 2; indices[1] = 0; indices[2] = 1;
    const void *later_indices = indices; g_build_frame = 1;
    assert(retain_indices(&later_indices, 3, &nverts) && nverts == 3);
    memset(indices, 0, sizeof indices);
    assert(((const uint16_t *)saved_indices)[0] == 0 && ((const uint16_t *)saved_indices)[2] == 2);
    assert(((const uint16_t *)later_indices)[0] == 2 && ((const uint16_t *)later_indices)[2] == 1);
    /* Recorded a10 cryo draw: the prior pool rejected 2,229 indices after
     * retaining 130,376. Preserve this draw and the previous frame's data. */
    uint16_t *cryo = malloc(2229 * sizeof *cryo); assert(cryo);
    for (unsigned i=0;i<2229;i++) cryo[i]=(uint16_t)(i % 743);
    g_index_used[1]=130376;
    const void *cryo_saved=cryo;
    assert(retain_indices(&cryo_saved,2229,&nverts) && nverts==743);
    memset(cryo,0,2229*sizeof *cryo);
    assert(((const uint16_t *)cryo_saved)[742]==742);
    assert(((const uint16_t *)saved_indices)[2]==2);
    free(cryo);
    g_index_used[1] = XV_FRAME_INDICES; const void *overflow_indices = indices;
    nverts = 123;
    assert(!retain_indices(&overflow_indices, 3, &nverts) && overflow_indices == indices && nverts == 123);
    g_index_used[1]=XV_FRAME_INDICES-8;
    assert(retain_indices(&overflow_indices,3,&nverts));
    assert(g_index_used[1]==XV_FRAME_INDICES);
    overflow_indices=indices;
    assert(!retain_indices(&overflow_indices,3,&nverts) && overflow_indices==indices);
    const void *owned = g_quad_indices + XV_QUAD_INDICES;
    assert(retain_indices(&owned, 3, &nverts) && owned == g_quad_indices + XV_QUAD_INDICES && nverts == 1);
    owned = g_quad_indices + XV_NUM_LISTS * XV_QUAD_INDICES - 1;
    nverts = 123;
    assert(!retain_indices(&owned, 3, &nverts) && nverts == 123);
    uint16_t seq[3] = {0, 1, 2}; g_seq_indices = seq; owned = seq;
    assert(retain_indices(&owned, 3, &nverts) && owned == seq && nverts == 3);
    assert(!retain_indices(&owned, XV_SEQ_INDICES + 1, &nverts));
    g_seq_indices = NULL;
    free(g_frame_indices); free(g_quad_indices); g_frame_indices = g_quad_indices = NULL;
    g_build_frame = 0; memset(g_index_used, 0, sizeof g_index_used);
    /* A subtractive material must not reuse an additive link with the same
     * factors; inverted color factors retain their inversion for alpha. */
    S.blend_enable = 1; S.color_mask = 15;
    S.src_blend = X_D3DBLEND_INVDESTCOLOR; S.dst_blend = X_D3DBLEND_INVSRCCOLOR;
    xv_d3d_SetRenderState_BlendOp(0x8006);
    unsigned add = blend_mode();
    xv_d3d_SetRenderState_BlendOp(0x800a);
    unsigned sub = blend_mode();
    assert(add != sub);
    SceGxmBlendInfo bi;
    assert(blend_info_for(sub, &bi) == &bi);
    assert(bi.colorFunc == SCE_GXM_BLEND_FUNC_SUBTRACT && bi.alphaFunc == bi.colorFunc);
    assert(bi.alphaSrc == SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_ALPHA);
    assert(bi.alphaDst == SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    xv_d3d_SetRenderState_BlendOp(0x800b);
    blend_info_for(blend_mode(), &bi);
    assert(bi.colorFunc == SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT);
    S.blend_enable = 0; S.color_mask = 7;
    blend_info_for(blend_mode(), &bi);
    assert(bi.colorFunc == SCE_GXM_BLEND_FUNC_ADD); /* disabled blending ignores old equation */
    /* Every canonical/texture variant must be reachable; a missing lookup must
     * never borrow the next material or another vertex program's entry. */
    for (unsigned i = 0; i < XV_PS_TABLE_COUNT; ++i) {
        const xv_ps_entry_t *e = &xv_ps_table[i];
        assert(ps_entry_for(e->vs_fnv, e->ps_key, e->c2d_mask) == (int)i);
        assert(ps_key_from_capture(e->ps_hash) == e->ps_key);
        assert((e->c2d_mask & ~e->cube_modes) == 0);
        assert((e->c2d_mask & e->cube_mask) == 0);
    }
    assert(ps_entry_for(0, 0, 0) == -1);
    assert(ps_entry_for(UINT32_MAX, UINT32_MAX, 15) == -1);
    assert(ps_key_from_capture(0) == 0);
    /* Menus consume twelve links before Blood Gulch's sky needs another. */
    xv_fshader_t *saved[40];
    for (unsigned i = 0; i < 40; i++) {
        saved[i] = fragment_for_ps(&g_vs[0], i, BLEND_ALPHA);
        assert(saved[i]);
    }
    for (unsigned i = 0; i < 40; i++)
        assert(fragment_for_ps(&g_vs[0], i, BLEND_ALPHA) == saved[i]);
    assert(loads == 40);
    assert(fragment_for_ps(&g_vs[1], 0, BLEND_ALPHA) != saved[0]);
    assert(fragment_for_ps(&g_vs[0], 0, BLEND_ADD) != saved[0]);
    fail_load = 1;
    assert(!fragment_for_ps(&g_vs[0], 41, BLEND_ALPHA));
    unsigned attempts = loads;
    fail_load = 0;
    assert(!fragment_for_ps(&g_vs[0], 41, BLEND_ALPHA));
    assert(loads == attempts); /* failed links are not retried each frame */
    assert(!fragment_for_ps(&g_vs[0], -1, BLEND_ALPHA));
    assert(!fragment_for_ps(&g_vs[0], XV_PS_TABLE_COUNT, BLEND_ALPHA));
    assert(!fragment_for_ps(&g_vs[0], 0, BLEND_MODES));
    /* Fill the bounded shared pool, exercising hash collisions. Existing links
     * must remain available without unloading programs still used by the GPU. */
    for (unsigned vs = 2; vs < XV_MAX_VS && g_ps_count < XV_PS_LINKS; vs++)
        for (unsigned i = 0; i < XV_PS_TABLE_COUNT && g_ps_count < XV_PS_LINKS; i++)
            assert(fragment_for_ps(&g_vs[vs], i, BLEND_ALPHA));
    assert(g_ps_count == XV_PS_LINKS);
    assert(!fragment_for_ps(&g_vs[XV_MAX_VS - 1], 0, BLEND_OPAQUE));
    for (unsigned i = 0; i < 40; i++)
        assert(fragment_for_ps(&g_vs[0], i, BLEND_ALPHA) == saved[i]);
    assert(fragment_for_ps_mode(&g_vs[0], 0, BLEND_ALPHA, 1) == saved[0]); /* Full specialized cache retains original. */
    assert(!unloads);
    unsigned successful = loads - 1;
    ps_links_shutdown();
    assert(unloads == successful && !g_ps_count);
    for (unsigned i = 0; i < XV_PS_BUCKETS; i++) assert(!g_ps_buckets[i]);
    assert(fragment_for_ps(&g_vs[0], 0, BLEND_ALPHA));
    ps_links_shutdown();
    assert(unloads == successful + 1);
    for (unsigned func=0;func<8;func++) {
        assert(!draw_needs_alpha_test(func<<8));
        assert(draw_needs_alpha_test((1u<<16)|(func<<8)) == (func!=7));
    }
    xv_fshader_t *base=fragment_for_ps(&g_vs[0],0,BLEND_OPAQUE);
    assert(base && !strstr(loaded_path,"_na.frag.gxp"));
    xv_fshader_t *special=fragment_for_ps_mode(&g_vs[0],0,BLEND_OPAQUE,1);
    assert(special && special!=base && strstr(loaded_path,"_na.frag.gxp"));
    attempts=loads;
    assert(fragment_for_ps(&g_vs[0],0,BLEND_OPAQUE)==base);
    assert(fragment_for_ps_mode(&g_vs[0],0,BLEND_OPAQUE,1)==special && loads==attempts);
    base=fragment_for_ps(&g_vs[0],1,BLEND_OPAQUE);
    fail_no_alpha=1;
    assert(fragment_for_ps_mode(&g_vs[0],1,BLEND_OPAQUE,1)==base);
    attempts=loads; fail_no_alpha=0;
    assert(fragment_for_ps_mode(&g_vs[0],1,BLEND_OPAQUE,1)==base && loads==attempts);
    ps_links_shutdown();
    unsigned material=0;while(xv_ps_table[material].ps_key!=0x154066FDu)material++;
    base=fragment_for_ps(&g_vs[0],material,BLEND_OPAQUE);
    special=fragment_for_ps_mode(&g_vs[0],material,BLEND_OPAQUE,1);
    xv_fshader_t *gt=fragment_for_ps_mode(&g_vs[0],material,BLEND_OPAQUE,2);
    assert(base && special && gt && gt!=base && gt!=special && strstr(loaded_path,"_gt.frag.gxp"));
    assert(base->alpha_test_mode==0 && special->alpha_test_mode==1 && gt->alpha_test_mode==2);
    attempts=loads;
    assert(fragment_for_ps_mode(&g_vs[0],material,BLEND_OPAQUE,2)==gt && loads==attempts);
    assert(!fragment_for_ps_mode(&g_vs[0],material,BLEND_OPAQUE,3));
    base=fragment_for_ps(&g_vs[0],material,BLEND_ADD);fail_gt=1;
    assert(fragment_for_ps_mode(&g_vs[0],material,BLEND_ADD,2)==base);
    attempts=loads;fail_gt=0;
    assert(fragment_for_ps_mode(&g_vs[0],material,BLEND_ADD,2)==base && loads==attempts);
    base=fragment_for_ps(&g_vs[0],material,BLEND_ALPHA);
    unsigned used_links=g_ps_count;g_ps_count=XV_PS_LINKS;
    assert(fragment_for_ps_mode(&g_vs[0],material,BLEND_ALPHA,2)==base);
    g_ps_count=used_links;ps_links_shutdown();
    puts("PASS: shader links survive menu/sky transitions, cache collisions, failures, full pool and shutdown");
}
