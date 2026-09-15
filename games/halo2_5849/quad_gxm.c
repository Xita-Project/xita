/* H2-only private RGB staging backend for the pinned movie-quad contract. */
#include "quad_gxm.h"
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
    SceGxmShaderPatcher *patcher=NULL;CHECK(sceGxmShaderPatcherCreate(&pp,&patcher));
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
