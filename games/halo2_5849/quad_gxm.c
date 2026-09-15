/* H2-only private RGB staging backend for the pinned movie-quad contract. */
#include "quad_gxm.h"
#include "dxt23_layout.h"
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
extern void xv_logf(const char *, ...);
#define CHECK(x) do { int err=(x); if(err<0){xv_logf("[h2/quad] FAIL %s = %08X\n",#x,err);return 0;} }while(0)
#define REQUIRE(x) do { if(!(x)){xv_logf("[h2/quad] FAIL %s\n",#x);return 0;} }while(0)
static void *alloc(unsigned size, int kind, unsigned *offset)
{
    size=(size+4095)&~4095u;
    int uid=sceKernelAllocMemBlock("h2_quad_staging",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,size,NULL);
    REQUIRE(uid>=0);void *ptr=NULL;CHECK(sceKernelGetMemBlockBase(uid,&ptr));
    if(kind==1)CHECK(sceGxmMapVertexUsseMemory(ptr,size,offset));
    else if(kind==2)CHECK(sceGxmMapFragmentUsseMemory(ptr,size,offset));
    else CHECK(sceGxmMapMemory(ptr,size,SCE_GXM_MEMORY_ATTRIB_READ|SCE_GXM_MEMORY_ATTRIB_WRITE));
    return ptr;
}
static void *host_alloc(void *unused,unsigned bytes){(void)unused;return malloc(bytes);}
static void host_free(void *unused,void *p){(void)unused;free(p);}
static void *load(const char *path, unsigned expected)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    void *data = NULL;
    int ok = !fseek(f, 0, SEEK_END);
    long size = ok ? ftell(f) : -1;
    if (size != (long)expected || !expected || expected > 65536 || fseek(f, 0, SEEK_SET)) ok = 0;
    if (ok) { data = malloc(expected); ok = data && fread(data, 1, expected, f) == expected; }
    if (fclose(f)) ok = 0;
    if (!ok) { free(data); xv_logf("[h2/quad] shader load failed %s\n", path); return NULL; }
    return data;
}
static const SceGxmProgramParameter *parameter(const SceGxmProgram *p,const char *name)
{
    const SceGxmProgramParameter *v=sceGxmProgramFindParameterByName(p,name);return v;
}

static SceGxmContext *ctx;
static SceGxmShaderPatcher *patcher;
static SceGxmRenderTarget *rt;
static SceGxmColorSurface color;
static SceGxmDepthStencilSurface depth;
static SceGxmTexture tex;
static SceGxmVertexProgram *vprog;
static SceGxmFragmentProgram *fprog;
static const SceGxmProgramParameter *uniform, *surface, *scale;
static unsigned sampler_index, notification;
static float *vertices;
static uint16_t *indices;
static uint32_t *texture, *target;
static int attempted, ready;
static h2_quad_contract contract;

const h2_quad_contract *h2_quad_gxm_contract(void)
{
    static int loaded;
    if (loaded) return &contract;
    FILE *f=fopen("app0:quad.contract.bin","rb");
    if (!f) { xv_logf("[h2/quad] missing private draw contract\n"); return NULL; }
    uint32_t header[2];
    int ok=fread(header,1,8,f)==8 && header[0]==0x43513248 && header[1]==1 &&
        fread(&contract,1,sizeof contract,f)==sizeof contract && fgetc(f)==EOF && !ferror(f);
    if (fclose(f)) ok=0;
    if (!ok) { memset(&contract,0,sizeof contract);xv_logf("[h2/quad] invalid private draw contract\n");return NULL; }
    loaded=1;return &contract;
}
static int initialize(void)
{
    SceGxmInitializeParams init={0};init.parameterBufferSize=SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    CHECK(sceGxmInitialize(&init));
    SceGxmContextParams cp={0};cp.hostMemSize=SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;cp.hostMem=malloc(cp.hostMemSize);REQUIRE(cp.hostMem);
    cp.vdmRingBufferMemSize=SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;cp.vdmRingBufferMem=alloc(cp.vdmRingBufferMemSize,0,NULL);REQUIRE(cp.vdmRingBufferMem);
    cp.vertexRingBufferMemSize=SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;cp.vertexRingBufferMem=alloc(cp.vertexRingBufferMemSize,0,NULL);REQUIRE(cp.vertexRingBufferMem);
    cp.fragmentRingBufferMemSize=SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;cp.fragmentRingBufferMem=alloc(cp.fragmentRingBufferMemSize,0,NULL);REQUIRE(cp.fragmentRingBufferMem);
    cp.fragmentUsseRingBufferMemSize=SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;cp.fragmentUsseRingBufferMem=alloc(cp.fragmentUsseRingBufferMemSize,2,&cp.fragmentUsseRingBufferOffset);
    REQUIRE(cp.fragmentUsseRingBufferMem);
    ctx=NULL;CHECK(sceGxmCreateContext(&cp,&ctx));
    SceGxmShaderPatcherParams pp={0};pp.hostAllocCallback=host_alloc;pp.hostFreeCallback=host_free;
    pp.bufferMemSize=512*1024;pp.bufferMem=alloc(pp.bufferMemSize,0,NULL);
    pp.vertexUsseMemSize=256*1024;pp.vertexUsseMem=alloc(pp.vertexUsseMemSize,1,&pp.vertexUsseOffset);
    pp.fragmentUsseMemSize=256*1024;pp.fragmentUsseMem=alloc(pp.fragmentUsseMemSize,2,&pp.fragmentUsseOffset);
    REQUIRE(pp.bufferMem && pp.vertexUsseMem && pp.fragmentUsseMem);
    patcher=NULL;CHECK(sceGxmShaderPatcherCreate(&pp,&patcher));
    const SceGxmProgram *vp=load("app0:quad.vert.gxp",1112),*fp=load("app0:quad.frag.gxp",400);
    REQUIRE(vp && fp);
    SceGxmShaderPatcherId vid,fid;CHECK(sceGxmShaderPatcherRegisterProgram(patcher,vp,&vid));CHECK(sceGxmShaderPatcherRegisterProgram(patcher,fp,&fid));
    const char *names[]={"IN.position","IN.color0","IN.texcoord0"};SceGxmVertexAttribute attr[3]={{0}};
    for(unsigned i=0;i<3;++i){attr[i].offset=i*16;attr[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attr[i].componentCount=4;const SceGxmProgramParameter *p=parameter(vp,names[i]);REQUIRE(p);attr[i].regIndex=sceGxmProgramParameterGetResourceIndex(p);}
    SceGxmVertexStream stream={.stride=48,.indexSource=SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    vprog=NULL; fprog=NULL;
    CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher,vid,attr,3,&stream,1,&vprog));
    SceGxmBlendInfo blend={.colorMask=SCE_GXM_COLOR_MASK_R|SCE_GXM_COLOR_MASK_G|SCE_GXM_COLOR_MASK_B,
        .colorFunc=SCE_GXM_BLEND_FUNC_ADD,.alphaFunc=SCE_GXM_BLEND_FUNC_ADD,
        .colorSrc=SCE_GXM_BLEND_FACTOR_ONE,.colorDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaSrc=SCE_GXM_BLEND_FACTOR_ONE,.alphaDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA};
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,&blend,vp,&fprog));
    uniform=parameter(vp,"c");surface=parameter(vp,"h2_surface");scale=parameter(fp,"h2_tex_scale");const SceGxmProgramParameter *sampler=parameter(fp,"tex0");
    REQUIRE(uniform && surface && scale && sampler);
    sampler_index=sceGxmProgramParameterGetResourceIndex(sampler);
    REQUIRE(sceGxmProgramParameterGetArraySize(uniform)==178);REQUIRE(sampler_index<4);
    REQUIRE(!sceGxmProgramFindParameterByName(fp,"tex1")&&!sceGxmProgramFindParameterByName(fp,"tex2"));
    vertices=alloc(4096,0,NULL);indices=alloc(4096,0,NULL);REQUIRE(vertices && indices);
    const uint16_t front[]={0,1,2,0,2,3};memcpy(indices,front,sizeof front);
    texture=alloc(640*480*4,0,NULL);target=alloc(640*480*4,0,NULL);
    void *depth_data=alloc(640*480*4,0,NULL);REQUIRE(texture && target && depth_data);
    SceGxmRenderTargetParams rp={.width=640,.height=480,.multisampleMode=SCE_GXM_MULTISAMPLE_NONE,.scenesPerFrame=1,.driverMemBlock=-1};
    CHECK(sceGxmCreateRenderTarget(&rp,&rt));
    /* This private staging surface initializes GXM's background mask through
     * the standard API. It is never copied to the guest depth attachment;
     * the accepted movie pipeline keeps depth/stencil writes disabled. */
    CHECK(sceGxmDepthStencilSurfaceInit(&depth,SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
        SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,640,depth_data,NULL));
    CHECK(sceGxmColorSurfaceInit(&color,SCE_GXM_COLOR_FORMAT_A8R8G8B8,SCE_GXM_COLOR_SURFACE_LINEAR,SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,640,480,640,target));
    CHECK(sceGxmTextureInitLinear(&tex,texture,SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1RGB,640,480,1));
    CHECK(sceGxmTextureSetMinFilter(&tex,SCE_GXM_TEXTURE_FILTER_LINEAR));CHECK(sceGxmTextureSetMagFilter(&tex,SCE_GXM_TEXTURE_FILTER_LINEAR));
    CHECK(sceGxmTextureSetUAddrMode(&tex,SCE_GXM_TEXTURE_ADDR_CLAMP));CHECK(sceGxmTextureSetVAddrMode(&tex,SCE_GXM_TEXTURE_ADDR_CLAMP));
    xv_logf("[h2/quad] GXM staging initialized 640x480 with private depth/mask; observed shader/combiner only\n");
    return 1;
}

/* Read-only measurements of already validated, completed staging buffers.
 * Save at most one nonblack input/output pair privately; no recurring image
 * writes, no guest mutation and no alteration of the rendering contract. */
static void trace_pixels(const h2_quad_request *request)
{
    static uint32_t count, captured;
    ++count;
    if (count > 4 && count % 60) return;
    uint32_t input_nonblack = 0, output_nonblack = 0, input_bits = 0, output_bits = 0;
    for (unsigned i = 0; i < 640 * 480; ++i) {
        uint32_t in = texture[i] & 0xFFFFFF, out = target[i] & 0xFFFFFF;
        input_nonblack += in != 0; output_nonblack += out != 0;
        input_bits |= in; output_bits |= out;
    }
    xv_logf("[h2/quad-pixels] draw=%u texture=%08X input_nonblack=%u output_nonblack=%u input_bits=%06X output_bits=%06X\n",
            count, request->texture.physical, input_nonblack, output_nonblack, input_bits, output_bits);
    if (!captured && input_nonblack) {
        captured = 1;
        const char *paths[] = {"ux0:data/xita-halo2/movie-input-first.bin", "ux0:data/xita-halo2/movie-output-first.bin"};
        const uint32_t *images[] = {texture, target};
        for (unsigned i = 0; i < 2; ++i) {
            FILE *file = fopen(paths[i], "wb");
            if (!file) continue;
            uint32_t header[] = {640, 480, 2560, count};
            int complete = fwrite(header, 1, sizeof header, file) == sizeof header &&
                           fwrite(images[i], 1, 640 * 480 * 4, file) == 640 * 480 * 4;
            int closed = fclose(file);
            xv_logf("[h2/quad-pixels] private first nonblack image=%u draw=%u complete=%d\n", i, count, complete && !closed);
        }
    }
}

const uint32_t *h2_quad_gxm_render(void *opaque, const h2_quad_request *request)
{
    (void)opaque;
    if (!request || request->texture.bytes!=640*480*4 || !request->texture.pixels || !request->constants) return NULL;
    if (!attempted) {attempted=1;ready=initialize();}
    if (!ready) return NULL;
    memcpy(vertices,request->vertices,sizeof request->vertices);
    memcpy(texture,request->texture.pixels,640*480*4);
    CHECK(sceGxmBeginScene(ctx,0,rt,NULL,NULL,NULL,&color,&depth));
    sceGxmSetViewportEnable(ctx,SCE_GXM_VIEWPORT_ENABLED);
    sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_NONE,0,0,639,479);
    sceGxmSetViewport(ctx,320,320,240,-240,0.0f,1.0f);sceGxmSetCullMode(ctx,SCE_GXM_CULL_CCW);
    sceGxmSetFrontFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
    sceGxmSetBackFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
    sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
    sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
    sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetVertexProgram(ctx,vprog);sceGxmSetFragmentProgram(ctx,fprog);
    void *vu,*fu;CHECK(sceGxmReserveVertexDefaultUniformBuffer(ctx,&vu));CHECK(sceGxmSetUniformDataF(vu,uniform,0,178*4,(const float *)request->constants));
    const float dims[]={640,480,16777215,0},inv[]={1.0f/640,1.0f/480,0,0};
    CHECK(sceGxmSetUniformDataF(vu,surface,0,4,dims));CHECK(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&fu));CHECK(sceGxmSetUniformDataF(fu,scale,0,4,inv));
    CHECK(sceGxmSetVertexStream(ctx,0,vertices));CHECK(sceGxmSetFragmentTexture(ctx,sampler_index,&tex));
    CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));SceGxmNotification done={sceGxmGetNotificationRegion(),++notification}; REQUIRE(done.address);
    CHECK(sceGxmEndScene(ctx,NULL,&done));CHECK(sceGxmNotificationWait(&done));sceGxmFinish(ctx);
    trace_pixels(request);
    return target;
}

#if H2_SCREEN_RENDER
/* Shares the existing H2 GXM context, target, mask and patcher. Each draw binds
 * all its own state. No second GXM initialization or CE renderer dependency. */
static h2_screen_contract screen_contract;
static SceGxmVertexProgram *screen_vprog;
static SceGxmFragmentProgram *screen_fprog, *screen_copyprog;
static const SceGxmProgramParameter *screen_factors;
static SceGxmTexture screen_tex[2], screen_copytex;
static uint32_t *screen_texture, *screen_destination;
static uint8_t *screen_blocks;
static unsigned screen_samplers[2], screen_copy_sampler;
static int screen_attempted, screen_ready;

const h2_screen_contract *h2_screen_gxm_contract(void)
{
    static int loaded;
    if(loaded)return &screen_contract;
    FILE *file=fopen("app0:screen.contract.bin","rb");if(!file)return NULL;
    uint32_t header[2];
    int ok=fread(header,1,8,file)==8&&header[0]==0x43533248&&header[1]==2&&
        fread(&screen_contract,1,sizeof screen_contract,file)==sizeof screen_contract&&
        fgetc(file)==EOF&&!ferror(file);
    if(fclose(file))ok=0;
    if(!ok){memset(&screen_contract,0,sizeof screen_contract);xv_logf("[h2/screen] invalid private contract\n");return NULL;}
    loaded=1;return &screen_contract;
}
static int screen_initialize(void)
{
    const SceGxmProgram *vp=load("app0:screen.vert.gxp",596),*fp=load("app0:screen.frag.gxp",1316),
        *copy=load("app0:screen.copy.frag.gxp",344);
    REQUIRE(vp&&fp&&copy);
    SceGxmShaderPatcherId vid,fid,cid;
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher,vp,&vid));
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher,fp,&fid));
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher,copy,&cid));
    const char *names[]={"IN.position","IN.blendweight","IN.normal","IN.color0","IN.color1","IN.fog","IN.psize"};
    SceGxmVertexAttribute attr[7]={{0}};
    for(unsigned i=0;i<7;++i){
        const SceGxmProgramParameter *p=parameter(vp,names[i]);REQUIRE(p);
        attr[i].offset=i*16;attr[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attr[i].componentCount=4;
        attr[i].regIndex=sceGxmProgramParameterGetResourceIndex(p);
    }
    SceGxmVertexStream stream={.stride=112,.indexSource=SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher,vid,attr,7,&stream,1,&screen_vprog));
    SceGxmBlendInfo blend={.colorMask=SCE_GXM_COLOR_MASK_R|SCE_GXM_COLOR_MASK_G|SCE_GXM_COLOR_MASK_B|SCE_GXM_COLOR_MASK_A,
        .colorFunc=SCE_GXM_BLEND_FUNC_ADD,.alphaFunc=SCE_GXM_BLEND_FUNC_ADD,
        .colorSrc=SCE_GXM_BLEND_FACTOR_ONE,.colorDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaSrc=SCE_GXM_BLEND_FACTOR_ZERO,.alphaDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA};
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,&blend,vp,&screen_fprog));
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,cid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&screen_copyprog));
    screen_factors=parameter(fp,"psc");REQUIRE(screen_factors&&sceGxmProgramParameterGetArraySize(screen_factors)==18);
    const SceGxmProgramParameter *p0=parameter(fp,"tex0"),*p2=parameter(fp,"tex2"),*pc=parameter(copy,"copy_source");
    REQUIRE(p0&&p2&&pc);
    screen_samplers[0]=sceGxmProgramParameterGetResourceIndex(p0);screen_samplers[1]=sceGxmProgramParameterGetResourceIndex(p2);
    screen_copy_sampler=sceGxmProgramParameterGetResourceIndex(pc);
    REQUIRE(screen_samplers[0]<4&&screen_samplers[1]<4&&screen_samplers[0]!=screen_samplers[1]&&screen_copy_sampler<4);
    screen_texture=alloc(640*480*4,0,NULL);screen_destination=alloc(640*480*4,0,NULL);screen_blocks=alloc(4096,0,NULL);
    REQUIRE(screen_texture&&screen_destination&&screen_blocks);
    CHECK(sceGxmTextureInitLinear(&screen_tex[0],screen_texture,SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB,640,480,1));
    CHECK(sceGxmTextureInitSwizzled(&screen_tex[1],screen_blocks,SCE_GXM_TEXTURE_FORMAT_UBC2_ABGR,8,8,1));
    CHECK(sceGxmTextureInitLinear(&screen_copytex,screen_destination,SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB,640,480,1));
    for(unsigned i=0;i<3;++i){
        SceGxmTexture *t=i==2?&screen_copytex:&screen_tex[i];
        CHECK(sceGxmTextureSetMinFilter(t,i==1?SCE_GXM_TEXTURE_FILTER_LINEAR:SCE_GXM_TEXTURE_FILTER_POINT));
        CHECK(sceGxmTextureSetMagFilter(t,i==1?SCE_GXM_TEXTURE_FILTER_LINEAR:SCE_GXM_TEXTURE_FILTER_POINT));
        CHECK(sceGxmTextureSetUAddrMode(t,SCE_GXM_TEXTURE_ADDR_CLAMP));CHECK(sceGxmTextureSetVAddrMode(t,SCE_GXM_TEXTURE_ADDR_CLAMP));
    }
    xv_logf("[h2/screen] initialized validated screen-effect shader/staging on existing H2 GXM context\n");
    return 1;
}
const uint32_t *h2_screen_gxm_render(void *opaque, const h2_screen_request *request)
{
    (void)opaque;
    if(!request||!request->destination||!request->texture0.pixels||request->texture0.bytes!=640*480*4||
        !request->texture2.blocks||request->texture2.bytes!=64)return NULL;
    if(!attempted){attempted=1;ready=initialize();}if(!ready)return NULL;
    if(!screen_attempted){screen_attempted=1;screen_ready=screen_initialize();}if(!screen_ready)return NULL;
    memcpy(vertices,request->vertices,sizeof request->vertices);
    memcpy(screen_texture,request->texture0.pixels,640*480*4);
    memcpy(screen_destination,request->destination,640*480*4);
    REQUIRE(h2_dxt23_gxm_8x8(request->texture2.blocks,64,screen_blocks,64));
    CHECK(sceGxmBeginScene(ctx,0,rt,NULL,NULL,NULL,&color,&depth));
    sceGxmSetViewportEnable(ctx,SCE_GXM_VIEWPORT_ENABLED);sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_NONE,0,0,639,479);
    sceGxmSetViewport(ctx,320,320,240,-240,0.0f,1.0f);sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
    sceGxmSetFrontFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);sceGxmSetBackFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
    sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
    sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
    sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetVertexProgram(ctx,screen_vprog);sceGxmSetFragmentProgram(ctx,screen_copyprog);
    CHECK(sceGxmSetVertexStream(ctx,0,vertices));CHECK(sceGxmSetFragmentTexture(ctx,screen_copy_sampler,&screen_copytex));
    CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));
    sceGxmSetFragmentProgram(ctx,screen_fprog);
    void *fu;CHECK(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&fu));
    CHECK(sceGxmSetUniformDataF(fu,screen_factors,0,72,(const float *)request->factors));
    for(unsigned i=0;i<2;++i)CHECK(sceGxmSetFragmentTexture(ctx,screen_samplers[i],&screen_tex[i]));
    CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));
    SceGxmNotification done={sceGxmGetNotificationRegion(),++notification};REQUIRE(done.address);
    CHECK(sceGxmEndScene(ctx,NULL,&done));CHECK(sceGxmNotificationWait(&done));sceGxmFinish(ctx);
    static unsigned count;
    if(++count<=4){
        unsigned nonblack=0;for(unsigned i=0;i<640*480;++i)nonblack+=(target[i]&0xFFFFFF)!=0;
        xv_logf("[h2/screen] staged=%u original_texture0=%08X texture2=%08X nonblack=%u first=%08X; not yet committed/presented\n",
                count,request->texture0.physical,request->texture2.physical,nonblack,target[0]);
    }
    return target;
}
#endif

#if H2_BC1_RENDER
/* The original second post-intro pass uses a smaller attachment and no
 * blending. Share only the existing H2 context, patcher and geometry buffers. */
static h2_bc1_contract bc1_contract;
static SceGxmVertexProgram *bc1_vprog;
static SceGxmFragmentProgram *bc1_fprog;
static const SceGxmProgramParameter *bc1_factors;
static SceGxmRenderTarget *bc1_rt;
static SceGxmColorSurface bc1_color;
static SceGxmDepthStencilSurface bc1_depth;
static SceGxmTexture bc1_tex[4];
static uint8_t *bc1_blocks[4];
static uint32_t *bc1_target;
static unsigned bc1_samplers[4];
static int bc1_attempted,bc1_ready;
const h2_bc1_contract *h2_bc1_gxm_contract(void)
{
    static int loaded;if(loaded)return &bc1_contract;
    FILE *file=fopen("app0:bc1.contract.bin","rb");if(!file)return NULL;
    uint32_t header[2];
    int ok=fread(header,1,8,file)==8&&header[0]==0x43423148&&header[1]==1&&
        fread(&bc1_contract,1,sizeof bc1_contract,file)==sizeof bc1_contract&&fgetc(file)==EOF&&!ferror(file);
    if(fclose(file))ok=0;
    if(!ok){memset(&bc1_contract,0,sizeof bc1_contract);xv_logf("[h2/bc1] invalid private contract\n");return NULL;}
    loaded=1;return &bc1_contract;
}
static int bc1_initialize(void)
{
    const SceGxmProgram *vp=load("app0:bc1.vert.gxp",596),*fp=load("app0:bc1.frag.gxp",892);
    REQUIRE(vp&&fp);SceGxmShaderPatcherId vid,fid;
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher,vp,&vid));CHECK(sceGxmShaderPatcherRegisterProgram(patcher,fp,&fid));
    const char *names[]={"IN.position","IN.blendweight","IN.normal","IN.color0","IN.color1","IN.fog","IN.psize"};
    SceGxmVertexAttribute attr[7]={{0}};
    for(unsigned i=0;i<7;++i){const SceGxmProgramParameter *p=parameter(vp,names[i]);REQUIRE(p);
        attr[i].offset=i*16;attr[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attr[i].componentCount=4;
        attr[i].regIndex=sceGxmProgramParameterGetResourceIndex(p);}
    SceGxmVertexStream stream={.stride=112,.indexSource=SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher,vid,attr,7,&stream,1,&bc1_vprog));
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&bc1_fprog));
    bc1_factors=parameter(fp,"psc");REQUIRE(bc1_factors&&sceGxmProgramParameterGetArraySize(bc1_factors)==18);
    unsigned used=0;
    for(unsigned i=0;i<4;++i){char name[8];snprintf(name,sizeof name,"tex%u",i);const SceGxmProgramParameter *p=parameter(fp,name);REQUIRE(p);
        bc1_samplers[i]=sceGxmProgramParameterGetResourceIndex(p);REQUIRE(bc1_samplers[i]<4&&!(used&(1u<<bc1_samplers[i])));used|=1u<<bc1_samplers[i];
        bc1_blocks[i]=alloc(4096,0,NULL);REQUIRE(bc1_blocks[i]);
        CHECK(sceGxmTextureInitSwizzled(&bc1_tex[i],bc1_blocks[i],SCE_GXM_TEXTURE_FORMAT_UBC1_ABGR,8,8,1));
        CHECK(sceGxmTextureSetMinFilter(&bc1_tex[i],SCE_GXM_TEXTURE_FILTER_LINEAR));CHECK(sceGxmTextureSetMagFilter(&bc1_tex[i],SCE_GXM_TEXTURE_FILTER_LINEAR));
        CHECK(sceGxmTextureSetUAddrMode(&bc1_tex[i],SCE_GXM_TEXTURE_ADDR_REPEAT));CHECK(sceGxmTextureSetVAddrMode(&bc1_tex[i],SCE_GXM_TEXTURE_ADDR_REPEAT));}
    bc1_target=alloc(320*240*4,0,NULL);void *mask=alloc(320*256*4,0,NULL);REQUIRE(bc1_target&&mask);
    SceGxmRenderTargetParams rp={.width=320,.height=240,.multisampleMode=SCE_GXM_MULTISAMPLE_NONE,.scenesPerFrame=1,.driverMemBlock=-1};
    CHECK(sceGxmCreateRenderTarget(&rp,&bc1_rt));
    CHECK(sceGxmDepthStencilSurfaceInit(&bc1_depth,SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,320,mask,NULL));
    CHECK(sceGxmColorSurfaceInit(&bc1_color,SCE_GXM_COLOR_FORMAT_A8R8G8B8,SCE_GXM_COLOR_SURFACE_LINEAR,SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,320,240,320,bc1_target));
    xv_logf("[h2/bc1] initialized validated 320x240 four-sampler pass on existing H2 context\n");return 1;
}
const uint32_t *h2_bc1_gxm_render(void *opaque,const h2_bc1_request *request)
{
    (void)opaque;if(!request||!request->destination)return NULL;
    for(unsigned i=0;i<4;++i)if(!request->textures[i].blocks||request->textures[i].bytes!=32)return NULL;
    if(!attempted){attempted=1;ready=initialize();}if(!ready)return NULL;
    if(!bc1_attempted){bc1_attempted=1;bc1_ready=bc1_initialize();}if(!bc1_ready)return NULL;
    memcpy(vertices,request->vertices,sizeof request->vertices);
    for(unsigned i=0;i<4;++i)REQUIRE(h2_dxt1_gxm_8x8(request->textures[i].blocks,32,bc1_blocks[i],32));
    CHECK(sceGxmBeginScene(ctx,0,bc1_rt,NULL,NULL,NULL,&bc1_color,&bc1_depth));
    sceGxmSetViewportEnable(ctx,SCE_GXM_VIEWPORT_ENABLED);sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_NONE,0,0,319,239);
    sceGxmSetViewport(ctx,160,160,120,-120,0,1);sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
    sceGxmSetFrontFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);sceGxmSetBackFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
    sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
    sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
    sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetVertexProgram(ctx,bc1_vprog);sceGxmSetFragmentProgram(ctx,bc1_fprog);CHECK(sceGxmSetVertexStream(ctx,0,vertices));
    void *fu;CHECK(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&fu));CHECK(sceGxmSetUniformDataF(fu,bc1_factors,0,72,(const float *)request->factors));
    for(unsigned i=0;i<4;++i)CHECK(sceGxmSetFragmentTexture(ctx,bc1_samplers[i],&bc1_tex[i]));
    CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));
    SceGxmNotification done={sceGxmGetNotificationRegion(),++notification};REQUIRE(done.address);
    CHECK(sceGxmEndScene(ctx,NULL,&done));CHECK(sceGxmNotificationWait(&done));sceGxmFinish(ctx);
    static unsigned count;
    if(++count<=4){unsigned nonblack=0;for(unsigned i=0;i<320*240;++i)nonblack+=(bc1_target[i]&0xFFFFFF)!=0;
        uint32_t uv[2];memcpy(uv,request->vertices[0].attribute[1],8);
        xv_logf("[h2/bc1] staged=%u original_texture0=%08X uv0_bits=%08X/%08X nonblack=%u first=%08X; not yet committed/presented\n",count,request->textures[0].physical,uv[0],uv[1],nonblack,bc1_target[0]);}
    return bc1_target;
}
#endif

#if H2_COMPOSITION_RENDER
/* Shares the existing H2 GXM context, target, mask and patcher. Each draw binds
 * all its own state. No second GXM initialization or CE renderer dependency. */
static h2_composition_contract composition_contract;
static SceGxmVertexProgram *composition_vprog;
static SceGxmFragmentProgram *composition_fprog, *composition_copyprog;
static const SceGxmProgramParameter *composition_factors;
static SceGxmTexture composition_tex[3], composition_copytex;
static uint32_t *composition_texture, *composition_texture3, *composition_destination;
static uint8_t *composition_blocks;
static unsigned composition_samplers[3], composition_copy_sampler;
static int composition_attempted, composition_ready;

const h2_composition_contract *h2_composition_gxm_contract(void)
{
    static int loaded;
    if(loaded)return &composition_contract;
    FILE *file=fopen("app0:composition.contract.bin","rb");if(!file){xv_logf("[h2/composition] private contract open failed\n");return NULL;}
    uint32_t header[2];
    int ok=fread(header,1,8,file)==8&&header[0]==0x43433248&&header[1]==1&&
        fread(&composition_contract,1,sizeof composition_contract,file)==sizeof composition_contract&&
        fgetc(file)==EOF&&!ferror(file);
    if(fclose(file))ok=0;
    if(!ok){memset(&composition_contract,0,sizeof composition_contract);xv_logf("[h2/composition] invalid private contract\n");return NULL;}
    loaded=1;xv_logf("[h2/composition] loaded complete private contract bytes=%u\n",(unsigned)sizeof composition_contract);return &composition_contract;
}
static int composition_initialize(void)
{
    const SceGxmProgram *vp=load("app0:composition.vert.gxp",596),*fp=load("app0:composition.frag.gxp",1528),
        *copy=load("app0:composition.copy.frag.gxp",344);
    REQUIRE(vp&&fp&&copy);
    SceGxmShaderPatcherId vid,fid,cid;
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher,vp,&vid));
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher,fp,&fid));
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher,copy,&cid));
    const char *names[]={"IN.position","IN.blendweight","IN.normal","IN.color0","IN.color1","IN.fog","IN.psize"};
    SceGxmVertexAttribute attr[7]={{0}};
    for(unsigned i=0;i<7;++i){
        const SceGxmProgramParameter *p=parameter(vp,names[i]);REQUIRE(p);
        attr[i].offset=i*16;attr[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attr[i].componentCount=4;
        attr[i].regIndex=sceGxmProgramParameterGetResourceIndex(p);
    }
    SceGxmVertexStream stream={.stride=112,.indexSource=SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher,vid,attr,7,&stream,1,&composition_vprog));
    SceGxmBlendInfo blend={.colorMask=SCE_GXM_COLOR_MASK_R|SCE_GXM_COLOR_MASK_G|SCE_GXM_COLOR_MASK_B|SCE_GXM_COLOR_MASK_A,
        .colorFunc=SCE_GXM_BLEND_FUNC_ADD,.alphaFunc=SCE_GXM_BLEND_FUNC_ADD,
        .colorSrc=SCE_GXM_BLEND_FACTOR_ONE,.colorDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaSrc=SCE_GXM_BLEND_FACTOR_ZERO,.alphaDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA};
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,&blend,vp,&composition_fprog));
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,cid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&composition_copyprog));
    composition_factors=parameter(fp,"psc");REQUIRE(composition_factors&&sceGxmProgramParameterGetArraySize(composition_factors)==18);
    const SceGxmProgramParameter *p0=parameter(fp,"tex0"),*p2=parameter(fp,"tex2"),*p3=parameter(fp,"tex3"),*pc=parameter(copy,"copy_source");
    REQUIRE(p0&&p2&&p3&&pc);
    composition_samplers[0]=sceGxmProgramParameterGetResourceIndex(p0);composition_samplers[1]=sceGxmProgramParameterGetResourceIndex(p2);
    composition_samplers[2]=sceGxmProgramParameterGetResourceIndex(p3);
    composition_copy_sampler=sceGxmProgramParameterGetResourceIndex(pc);
    REQUIRE(composition_samplers[0]<4&&composition_samplers[1]<4&&composition_samplers[0]!=composition_samplers[1]&&composition_samplers[2]<4&&composition_samplers[2]!=composition_samplers[0]&&composition_samplers[2]!=composition_samplers[1]&&composition_copy_sampler<4);
    composition_texture=alloc(640*480*4,0,NULL);composition_destination=alloc(640*480*4,0,NULL);composition_blocks=alloc(4096,0,NULL);
    composition_texture3=alloc(320*240*4,0,NULL);
    REQUIRE(composition_texture&&composition_texture3&&composition_destination&&composition_blocks);
    CHECK(sceGxmTextureInitLinear(&composition_tex[0],composition_texture,SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB,640,480,1));
    CHECK(sceGxmTextureInitSwizzled(&composition_tex[1],composition_blocks,SCE_GXM_TEXTURE_FORMAT_UBC2_ABGR,8,8,1));
    CHECK(sceGxmTextureInitLinear(&composition_copytex,composition_destination,SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB,640,480,1));
    CHECK(sceGxmTextureInitLinear(&composition_tex[2],composition_texture3,SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB,320,240,1));
    for(unsigned i=0;i<4;++i){
        SceGxmTexture *t=i==3?&composition_copytex:&composition_tex[i];
        CHECK(sceGxmTextureSetMinFilter(t,(i==1||i==2)?SCE_GXM_TEXTURE_FILTER_LINEAR:SCE_GXM_TEXTURE_FILTER_POINT));
        CHECK(sceGxmTextureSetMagFilter(t,(i==1||i==2)?SCE_GXM_TEXTURE_FILTER_LINEAR:SCE_GXM_TEXTURE_FILTER_POINT));
        CHECK(sceGxmTextureSetUAddrMode(t,SCE_GXM_TEXTURE_ADDR_CLAMP));CHECK(sceGxmTextureSetVAddrMode(t,SCE_GXM_TEXTURE_ADDR_CLAMP));
    }
    xv_logf("[h2/composition] initialized validated composition-effect shader/staging on existing H2 GXM context\n");
    return 1;
}
const uint32_t *h2_composition_gxm_render(void *opaque, const h2_composition_request *request)
{
    (void)opaque;
    if(!request||!request->destination||!request->texture0.pixels||request->texture0.bytes!=640*480*4||
        !request->texture2.blocks||request->texture2.bytes!=64||
        !request->texture3.pixels||request->texture3.bytes!=320*240*4)return NULL;
    if(!attempted){attempted=1;ready=initialize();}if(!ready)return NULL;
    if(!composition_attempted){composition_attempted=1;composition_ready=composition_initialize();}if(!composition_ready)return NULL;
    memcpy(vertices,request->vertices,sizeof request->vertices);
    memcpy(composition_texture,request->texture0.pixels,640*480*4);
    memcpy(composition_destination,request->destination,640*480*4);
    memcpy(composition_texture3,request->texture3.pixels,320*240*4);
    REQUIRE(h2_dxt23_gxm_8x8(request->texture2.blocks,64,composition_blocks,64));
    CHECK(sceGxmBeginScene(ctx,0,rt,NULL,NULL,NULL,&color,&depth));
    sceGxmSetViewportEnable(ctx,SCE_GXM_VIEWPORT_ENABLED);sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_NONE,0,0,639,479);
    sceGxmSetViewport(ctx,320,320,240,-240,0.0f,1.0f);sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
    sceGxmSetFrontFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);sceGxmSetBackFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
    sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
    sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
    sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetVertexProgram(ctx,composition_vprog);sceGxmSetFragmentProgram(ctx,composition_copyprog);
    CHECK(sceGxmSetVertexStream(ctx,0,vertices));CHECK(sceGxmSetFragmentTexture(ctx,composition_copy_sampler,&composition_copytex));
    CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));
    sceGxmSetFragmentProgram(ctx,composition_fprog);
    void *fu;CHECK(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&fu));
    CHECK(sceGxmSetUniformDataF(fu,composition_factors,0,72,(const float *)request->factors));
    for(unsigned i=0;i<3;++i)CHECK(sceGxmSetFragmentTexture(ctx,composition_samplers[i],&composition_tex[i]));
    CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));
    SceGxmNotification done={sceGxmGetNotificationRegion(),++notification};REQUIRE(done.address);
    CHECK(sceGxmEndScene(ctx,NULL,&done));CHECK(sceGxmNotificationWait(&done));sceGxmFinish(ctx);
    static unsigned count;
    if(++count<=4){
        unsigned nonblack=0;for(unsigned i=0;i<640*480;++i)nonblack+=(target[i]&0xFFFFFF)!=0;
        xv_logf("[h2/composition] staged=%u original_texture0=%08X texture3=%08X nonblack=%u first=%08X; not yet committed/presented\n",
                count,request->texture0.physical,request->texture3.physical,nonblack,target[0]);
    }
    return target;
}
#endif

#if H2_THRESHOLD_RENDER
/* The original threshold/downsample uses a clipped 160x120 attachment
 * without blending. Share the existing H2 context, patcher and geometry buffers. */
static h2_threshold_contract threshold_contract;
static SceGxmVertexProgram *threshold_vprog;
static SceGxmFragmentProgram *threshold_fprog;
static const SceGxmProgramParameter *threshold_factors;
static SceGxmRenderTarget *threshold_rt;
static SceGxmColorSurface threshold_color;
static SceGxmDepthStencilSurface threshold_depth;
static SceGxmTexture threshold_tex[4];
static uint8_t *threshold_pixels[4];
static uint32_t *threshold_target;
static unsigned threshold_samplers[4];
static int threshold_attempted,threshold_ready;
const h2_threshold_contract *h2_threshold_gxm_contract(void)
{
    static int loaded;if(loaded)return &threshold_contract;
    FILE *file=fopen("app0:threshold.contract.bin","rb");if(!file)return NULL;
    uint32_t header[2];
    int ok=fread(header,1,8,file)==8&&header[0]==0x43543248&&header[1]==1&&
        fread(&threshold_contract,1,sizeof threshold_contract,file)==sizeof threshold_contract&&fgetc(file)==EOF&&!ferror(file);
    if(fclose(file))ok=0;
    if(!ok){memset(&threshold_contract,0,sizeof threshold_contract);xv_logf("[h2/threshold] invalid private contract\n");return NULL;}
    loaded=1;return &threshold_contract;
}
static int threshold_initialize(void)
{
    const SceGxmProgram *vp=load("app0:threshold.vert.gxp",596),*fp=load("app0:threshold.frag.gxp",1012);
    REQUIRE(vp&&fp);SceGxmShaderPatcherId vid,fid;
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher,vp,&vid));CHECK(sceGxmShaderPatcherRegisterProgram(patcher,fp,&fid));
    const char *names[]={"IN.position","IN.blendweight","IN.normal","IN.color0","IN.color1","IN.fog","IN.psize"};
    SceGxmVertexAttribute attr[7]={{0}};
    for(unsigned i=0;i<7;++i){const SceGxmProgramParameter *p=parameter(vp,names[i]);REQUIRE(p);
        attr[i].offset=i*16;attr[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attr[i].componentCount=4;
        attr[i].regIndex=sceGxmProgramParameterGetResourceIndex(p);}
    SceGxmVertexStream stream={.stride=112,.indexSource=SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher,vid,attr,7,&stream,1,&threshold_vprog));
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&threshold_fprog));
    threshold_factors=parameter(fp,"psc");REQUIRE(threshold_factors&&sceGxmProgramParameterGetArraySize(threshold_factors)==18);
    unsigned used=0;
    for(unsigned i=0;i<4;++i){char name[8];snprintf(name,sizeof name,"tex%u",i);const SceGxmProgramParameter *p=parameter(fp,name);REQUIRE(p);
        threshold_samplers[i]=sceGxmProgramParameterGetResourceIndex(p);REQUIRE(threshold_samplers[i]<4&&!(used&(1u<<threshold_samplers[i])));used|=1u<<threshold_samplers[i];
        threshold_pixels[i]=alloc(640*480*4,0,NULL);REQUIRE(threshold_pixels[i]);
        CHECK(sceGxmTextureInitLinear(&threshold_tex[i],threshold_pixels[i],SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB,640,480,1));
        CHECK(sceGxmTextureSetMinFilter(&threshold_tex[i],SCE_GXM_TEXTURE_FILTER_LINEAR));CHECK(sceGxmTextureSetMagFilter(&threshold_tex[i],SCE_GXM_TEXTURE_FILTER_LINEAR));
        CHECK(sceGxmTextureSetUAddrMode(&threshold_tex[i],SCE_GXM_TEXTURE_ADDR_CLAMP));CHECK(sceGxmTextureSetVAddrMode(&threshold_tex[i],SCE_GXM_TEXTURE_ADDR_CLAMP));}
    threshold_target=alloc(160*120*4,0,NULL);void *mask=alloc(160*128*4,0,NULL);REQUIRE(threshold_target&&mask);
    SceGxmRenderTargetParams rp={.width=160,.height=120,.multisampleMode=SCE_GXM_MULTISAMPLE_NONE,.scenesPerFrame=1,.driverMemBlock=-1};
    CHECK(sceGxmCreateRenderTarget(&rp,&threshold_rt));
    CHECK(sceGxmDepthStencilSurfaceInit(&threshold_depth,SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,160,mask,NULL));
    CHECK(sceGxmColorSurfaceInit(&threshold_color,SCE_GXM_COLOR_FORMAT_A8R8G8B8,SCE_GXM_COLOR_SURFACE_LINEAR,SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,160,120,160,threshold_target));
    xv_logf("[h2/threshold] initialized validated 160x120 four-sampler pass on existing H2 context\n");return 1;
}
const uint32_t *h2_threshold_gxm_render(void *opaque,const h2_threshold_request *request)
{
    (void)opaque;if(!request||!request->destination)return NULL;
    for(unsigned i=0;i<4;++i)if(!request->textures[i].pixels||request->textures[i].bytes!=640*480*4)return NULL;
    if(!attempted){attempted=1;ready=initialize();}if(!ready)return NULL;
    if(!threshold_attempted){threshold_attempted=1;threshold_ready=threshold_initialize();}if(!threshold_ready)return NULL;
    memcpy(vertices,request->vertices,sizeof request->vertices);
    for(unsigned i=0;i<4;++i)memcpy(threshold_pixels[i],request->textures[i].pixels,640*480*4);
    CHECK(sceGxmBeginScene(ctx,0,threshold_rt,NULL,NULL,NULL,&threshold_color,&threshold_depth));
    sceGxmSetViewportEnable(ctx,SCE_GXM_VIEWPORT_ENABLED);sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_NONE,0,0,159,119);
    sceGxmSetViewport(ctx,80,80,60,-60,0,1);sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
    sceGxmSetFrontFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);sceGxmSetBackFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
    sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
    sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
    sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetVertexProgram(ctx,threshold_vprog);sceGxmSetFragmentProgram(ctx,threshold_fprog);CHECK(sceGxmSetVertexStream(ctx,0,vertices));
    void *fu;CHECK(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&fu));CHECK(sceGxmSetUniformDataF(fu,threshold_factors,0,72,(const float *)request->factors));
    for(unsigned i=0;i<4;++i)CHECK(sceGxmSetFragmentTexture(ctx,threshold_samplers[i],&threshold_tex[i]));
    CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));
    SceGxmNotification done={sceGxmGetNotificationRegion(),++notification};REQUIRE(done.address);
    CHECK(sceGxmEndScene(ctx,NULL,&done));CHECK(sceGxmNotificationWait(&done));sceGxmFinish(ctx);
    static unsigned count;
    if(++count<=4){unsigned nonblack=0;for(unsigned i=0;i<160*120;++i)nonblack+=(threshold_target[i]&0xFFFFFF)!=0;
        uint32_t uv[2];memcpy(uv,request->vertices[0].attribute[1],8);
        xv_logf("[h2/threshold] staged=%u original_texture0=%08X uv0_bits=%08X/%08X nonblack=%u first=%08X; not yet committed/presented\n",count,request->textures[0].physical,uv[0],uv[1],nonblack,threshold_target[0]);}
    return threshold_target;
}
#endif

#if H2_BLUR_RENDER
/* The original averaging pass uses a clipped 160x120 attachment
 * without blending. Share the existing H2 context, patcher and geometry buffers. */
static h2_blur_contract blur_contract;
static SceGxmVertexProgram *blur_vprog;
static SceGxmFragmentProgram *blur_fprog;
static const SceGxmProgramParameter *blur_factors;
static SceGxmRenderTarget *blur_rt;
static SceGxmColorSurface blur_color;
static SceGxmDepthStencilSurface blur_depth;
static SceGxmTexture blur_tex[4];
static uint8_t *blur_pixels[4];
static uint32_t *blur_target;
static unsigned blur_samplers[4];
static int blur_attempted,blur_ready;
const h2_blur_contract *h2_blur_gxm_contract(void)
{
    static int loaded;if(loaded)return &blur_contract;
    FILE *file=fopen("app0:blur.contract.bin","rb");if(!file)return NULL;
    uint32_t header[2];
    int ok=fread(header,1,8,file)==8&&header[0]==0x434C3248&&header[1]==1&&
        fread(&blur_contract,1,sizeof blur_contract,file)==sizeof blur_contract&&fgetc(file)==EOF&&!ferror(file);
    if(fclose(file))ok=0;
    if(!ok){memset(&blur_contract,0,sizeof blur_contract);xv_logf("[h2/blur] invalid private contract\n");return NULL;}
    loaded=1;return &blur_contract;
}
static int blur_initialize(void)
{
    const SceGxmProgram *vp=load("app0:blur.vert.gxp",596),*fp=load("app0:blur.frag.gxp",1092);
    REQUIRE(vp&&fp);SceGxmShaderPatcherId vid,fid;
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher,vp,&vid));CHECK(sceGxmShaderPatcherRegisterProgram(patcher,fp,&fid));
    const char *names[]={"IN.position","IN.blendweight","IN.normal","IN.color0","IN.color1","IN.fog","IN.psize"};
    SceGxmVertexAttribute attr[7]={{0}};
    for(unsigned i=0;i<7;++i){const SceGxmProgramParameter *p=parameter(vp,names[i]);REQUIRE(p);
        attr[i].offset=i*16;attr[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attr[i].componentCount=4;
        attr[i].regIndex=sceGxmProgramParameterGetResourceIndex(p);}
    SceGxmVertexStream stream={.stride=112,.indexSource=SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher,vid,attr,7,&stream,1,&blur_vprog));
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&blur_fprog));
    blur_factors=parameter(fp,"psc");REQUIRE(blur_factors&&sceGxmProgramParameterGetArraySize(blur_factors)==18);
    unsigned used=0;
    for(unsigned i=0;i<4;++i){char name[8];snprintf(name,sizeof name,"tex%u",i);const SceGxmProgramParameter *p=parameter(fp,name);REQUIRE(p);
        blur_samplers[i]=sceGxmProgramParameterGetResourceIndex(p);REQUIRE(blur_samplers[i]<4&&!(used&(1u<<blur_samplers[i])));used|=1u<<blur_samplers[i];
        blur_pixels[i]=alloc(160*120*4,0,NULL);REQUIRE(blur_pixels[i]);
        CHECK(sceGxmTextureInitLinear(&blur_tex[i],blur_pixels[i],SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB,160,120,1));
        CHECK(sceGxmTextureSetMinFilter(&blur_tex[i],SCE_GXM_TEXTURE_FILTER_LINEAR));CHECK(sceGxmTextureSetMagFilter(&blur_tex[i],SCE_GXM_TEXTURE_FILTER_LINEAR));
        CHECK(sceGxmTextureSetUAddrMode(&blur_tex[i],SCE_GXM_TEXTURE_ADDR_CLAMP));CHECK(sceGxmTextureSetVAddrMode(&blur_tex[i],SCE_GXM_TEXTURE_ADDR_CLAMP));}
    blur_target=alloc(160*120*4,0,NULL);void *mask=alloc(160*128*4,0,NULL);REQUIRE(blur_target&&mask);
    SceGxmRenderTargetParams rp={.width=160,.height=120,.multisampleMode=SCE_GXM_MULTISAMPLE_NONE,.scenesPerFrame=1,.driverMemBlock=-1};
    CHECK(sceGxmCreateRenderTarget(&rp,&blur_rt));
    CHECK(sceGxmDepthStencilSurfaceInit(&blur_depth,SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,160,mask,NULL));
    CHECK(sceGxmColorSurfaceInit(&blur_color,SCE_GXM_COLOR_FORMAT_A8R8G8B8,SCE_GXM_COLOR_SURFACE_LINEAR,SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,160,120,160,blur_target));
    xv_logf("[h2/blur] initialized validated 160x120 four-sampler pass on existing H2 context\n");return 1;
}
const uint32_t *h2_blur_gxm_render(void *opaque,const h2_blur_request *request)
{
    (void)opaque;if(!request||!request->destination)return NULL;
    for(unsigned i=0;i<4;++i)if(!request->textures[i].pixels||request->textures[i].bytes!=160*120*4)return NULL;
    if(!attempted){attempted=1;ready=initialize();}if(!ready)return NULL;
    if(!blur_attempted){blur_attempted=1;blur_ready=blur_initialize();}if(!blur_ready)return NULL;
    memcpy(vertices,request->vertices,sizeof request->vertices);
    for(unsigned i=0;i<4;++i)memcpy(blur_pixels[i],request->textures[i].pixels,160*120*4);
    CHECK(sceGxmBeginScene(ctx,0,blur_rt,NULL,NULL,NULL,&blur_color,&blur_depth));
    sceGxmSetViewportEnable(ctx,SCE_GXM_VIEWPORT_ENABLED);sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_NONE,0,0,159,119);
    sceGxmSetViewport(ctx,80,80,60,-60,0,1);sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
    sceGxmSetFrontFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);sceGxmSetBackFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
    sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
    sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
    sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetVertexProgram(ctx,blur_vprog);sceGxmSetFragmentProgram(ctx,blur_fprog);CHECK(sceGxmSetVertexStream(ctx,0,vertices));
    void *fu;CHECK(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&fu));CHECK(sceGxmSetUniformDataF(fu,blur_factors,0,72,(const float *)request->factors));
    for(unsigned i=0;i<4;++i)CHECK(sceGxmSetFragmentTexture(ctx,blur_samplers[i],&blur_tex[i]));
    CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));
    SceGxmNotification done={sceGxmGetNotificationRegion(),++notification};REQUIRE(done.address);
    CHECK(sceGxmEndScene(ctx,NULL,&done));CHECK(sceGxmNotificationWait(&done));sceGxmFinish(ctx);
    static unsigned count;
    if(++count<=4){unsigned nonblack=0;for(unsigned i=0;i<160*120;++i)nonblack+=(blur_target[i]&0xFFFFFF)!=0;
        uint32_t uv[2];memcpy(uv,request->vertices[0].attribute[1],8);
        xv_logf("[h2/blur] staged=%u original_texture0=%08X uv0_bits=%08X/%08X nonblack=%u first=%08X; not yet committed/presented\n",count,request->textures[0].physical,uv[0],uv[1],nonblack,blur_target[0]);}
    return blur_target;
}
#endif
