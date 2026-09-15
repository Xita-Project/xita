/* Isolated screen-effect GPU probe. Private shaders and captured inputs are
 * supplied externally. No Xbox commands execute; fixtures are not a menu. */
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dxt23_layout.h"
#define CHECK(x) do { int err=(x); if(err<0){sceClibPrintf("[screen-probe] FAIL %s = %08X\n",#x,err);sceKernelExitProcess(1);} }while(0)
#define REQUIRE(x) do { if(!(x)){sceClibPrintf("[screen-probe] FAIL %s\n",#x);sceKernelExitProcess(1);} }while(0)
static void *alloc(unsigned size, int kind, unsigned *offset)
{
    size=(size+4095)&~4095u;
    int uid=sceKernelAllocMemBlock("h2_screen_probe",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,size,NULL);
    REQUIRE(uid>=0);void *ptr=NULL;CHECK(sceKernelGetMemBlockBase(uid,&ptr));
    if(kind==1)CHECK(sceGxmMapVertexUsseMemory(ptr,size,offset));
    else if(kind==2)CHECK(sceGxmMapFragmentUsseMemory(ptr,size,offset));
    else CHECK(sceGxmMapMemory(ptr,size,SCE_GXM_MEMORY_ATTRIB_READ|SCE_GXM_MEMORY_ATTRIB_WRITE));
    return ptr;
}
static void *host_alloc(void *unused,unsigned bytes){(void)unused;return malloc(bytes);}
static void host_free(void *unused,void *p){(void)unused;free(p);}
static void *load(const char *path,unsigned expected)
{
    FILE *f=fopen(path,"rb");REQUIRE(f);REQUIRE(!fseek(f,0,SEEK_END));long n=ftell(f);REQUIRE(n>0&&n<65536);
    REQUIRE(!expected||(unsigned)n==expected);rewind(f);void *p=malloc(n);REQUIRE(p);REQUIRE(fread(p,1,n,f)==(unsigned)n);REQUIRE(!fclose(f));return p;
}
static const SceGxmProgramParameter *parameter(const SceGxmProgram *p,const char *name)
{
    const SceGxmProgramParameter *v=sceGxmProgramFindParameterByName(p,name);REQUIRE(v);return v;
}
int main(void)
{
    SceGxmInitializeParams init={0};init.parameterBufferSize=SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    CHECK(sceGxmInitialize(&init));
    SceGxmContextParams cp={0};cp.hostMemSize=SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;cp.hostMem=malloc(cp.hostMemSize);REQUIRE(cp.hostMem);
    cp.vdmRingBufferMemSize=SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;cp.vdmRingBufferMem=alloc(cp.vdmRingBufferMemSize,0,NULL);
    cp.vertexRingBufferMemSize=SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;cp.vertexRingBufferMem=alloc(cp.vertexRingBufferMemSize,0,NULL);
    cp.fragmentRingBufferMemSize=SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;cp.fragmentRingBufferMem=alloc(cp.fragmentRingBufferMemSize,0,NULL);
    cp.fragmentUsseRingBufferMemSize=SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;cp.fragmentUsseRingBufferMem=alloc(cp.fragmentUsseRingBufferMemSize,2,&cp.fragmentUsseRingBufferOffset);
    SceGxmContext *ctx=NULL;CHECK(sceGxmCreateContext(&cp,&ctx));
    SceGxmShaderPatcherParams pp={0};pp.hostAllocCallback=host_alloc;pp.hostFreeCallback=host_free;
    pp.bufferMemSize=512*1024;pp.bufferMem=alloc(pp.bufferMemSize,0,NULL);
    pp.vertexUsseMemSize=256*1024;pp.vertexUsseMem=alloc(pp.vertexUsseMemSize,1,&pp.vertexUsseOffset);
    pp.fragmentUsseMemSize=256*1024;pp.fragmentUsseMem=alloc(pp.fragmentUsseMemSize,2,&pp.fragmentUsseOffset);
    SceGxmShaderPatcher *patcher=NULL;CHECK(sceGxmShaderPatcherCreate(&pp,&patcher));
    const SceGxmProgram *vp=load("app0:screen.vert.gxp",0),*fp=load("app0:screen.frag.gxp",0);
    SceGxmShaderPatcherId vid,fid;CHECK(sceGxmShaderPatcherRegisterProgram(patcher,vp,&vid));CHECK(sceGxmShaderPatcherRegisterProgram(patcher,fp,&fid));
    const char *names[]={"IN.position","IN.blendweight","IN.normal","IN.color0","IN.color1","IN.fog","IN.psize"};
    SceGxmVertexAttribute attr[7]={{0}};
    for(unsigned i=0;i<7;++i){attr[i].offset=i*16;attr[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attr[i].componentCount=4;attr[i].regIndex=sceGxmProgramParameterGetResourceIndex(parameter(vp,names[i]));}
    SceGxmVertexStream stream={.stride=112,.indexSource=SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    SceGxmVertexProgram *vprog=NULL;SceGxmFragmentProgram *fprog=NULL;
    CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher,vid,attr,7,&stream,1,&vprog));
    SceGxmBlendInfo blend={.colorMask=SCE_GXM_COLOR_MASK_R|SCE_GXM_COLOR_MASK_G|SCE_GXM_COLOR_MASK_B|SCE_GXM_COLOR_MASK_A,
        .colorFunc=SCE_GXM_BLEND_FUNC_ADD,.alphaFunc=SCE_GXM_BLEND_FUNC_ADD,
        .colorSrc=SCE_GXM_BLEND_FACTOR_ONE,.colorDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaSrc=SCE_GXM_BLEND_FACTOR_ZERO,.alphaDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA};
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,&blend,vp,&fprog));
    const SceGxmProgram *copy=load("app0:screen.copy.frag.gxp",0);
    SceGxmShaderPatcherId copyid;SceGxmFragmentProgram *copyprog=NULL;
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher,copy,&copyid));
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,copyid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&copyprog));
    unsigned copy_sampler=sceGxmProgramParameterGetResourceIndex(parameter(copy,"copy_source"));REQUIRE(copy_sampler<4);
    const SceGxmProgramParameter *uniform=parameter(fp,"psc"),*sampler0=parameter(fp,"tex0"),*sampler2=parameter(fp,"tex2");
    unsigned samplers[]={sceGxmProgramParameterGetResourceIndex(sampler0),sceGxmProgramParameterGetResourceIndex(sampler2)};
    REQUIRE(samplers[0]<4 && samplers[1]<4 && samplers[0]!=samplers[1]);
    REQUIRE(sceGxmProgramParameterGetArraySize(uniform)==18);
    float *constants=load("app0:screen.constants.bin",72*4),*original_vertices=load("app0:screen.vertices.bin",4*112);
    uint8_t *original_blocks=load("app0:screen.texture2.bin",64);
    float *vertices=alloc(4096,0,NULL);uint16_t *indices=alloc(4096,0,NULL);
    const uint16_t front[]={0,1,2,0,2,3},back[]={0,2,1,0,3,2};
    uint32_t *texture=alloc(640*480*4,0,NULL),*target=alloc(640*480*4,0,NULL);
    uint32_t *destination=alloc(640*480*4,0,NULL);
    uint8_t *blocks=alloc(4096,0,NULL);
    SceGxmRenderTargetParams rp={.width=640,.height=480,.multisampleMode=SCE_GXM_MULTISAMPLE_NONE,.scenesPerFrame=1,.driverMemBlock=-1};
    SceGxmRenderTarget *rt=NULL;CHECK(sceGxmCreateRenderTarget(&rp,&rt));
    SceGxmDepthStencilSurface depth;void *depth_data=alloc(640*480*4,0,NULL);
    CHECK(sceGxmDepthStencilSurfaceInit(&depth,SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
        SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,640,depth_data,NULL));
    SceGxmColorSurface color;CHECK(sceGxmColorSurfaceInit(&color,SCE_GXM_COLOR_FORMAT_A8R8G8B8,SCE_GXM_COLOR_SURFACE_LINEAR,SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,640,480,640,target));
    SceGxmTexture tex[2];CHECK(sceGxmTextureInitLinear(&tex[0],texture,SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB,640,480,1));
    SceGxmTexture copytex;CHECK(sceGxmTextureInitLinear(&copytex,destination,SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB,640,480,1));
    CHECK(sceGxmTextureSetMinFilter(&copytex,SCE_GXM_TEXTURE_FILTER_POINT));CHECK(sceGxmTextureSetMagFilter(&copytex,SCE_GXM_TEXTURE_FILTER_POINT));
    CHECK(sceGxmTextureSetUAddrMode(&copytex,SCE_GXM_TEXTURE_ADDR_CLAMP));CHECK(sceGxmTextureSetVAddrMode(&copytex,SCE_GXM_TEXTURE_ADDR_CLAMP));
    // Exactly 8x8 BC2; convert its four row-ordered blocks to GXM's Y-first
    // block layout below. No larger compressed layout is assumed here.
    CHECK(sceGxmTextureInitSwizzled(&tex[1],blocks,SCE_GXM_TEXTURE_FORMAT_UBC2_ABGR,8,8,1));
    for(unsigned i=0;i<2;++i){
        CHECK(sceGxmTextureSetMinFilter(&tex[i],i?SCE_GXM_TEXTURE_FILTER_LINEAR:SCE_GXM_TEXTURE_FILTER_POINT));
        CHECK(sceGxmTextureSetMagFilter(&tex[i],i?SCE_GXM_TEXTURE_FILTER_LINEAR:SCE_GXM_TEXTURE_FILTER_POINT));
        CHECK(sceGxmTextureSetUAddrMode(&tex[i],SCE_GXM_TEXTURE_ADDR_CLAMP));CHECK(sceGxmTextureSetVAddrMode(&tex[i],SCE_GXM_TEXTURE_ADDR_CLAMP));
    }
    for(unsigned test=0;test<8;++test){
        memcpy(vertices,original_vertices,4*112);memcpy(blocks,original_blocks,64);
        for(unsigned i=0;i<640*480;++i){unsigned x=i%640,y=i/640;
            texture[i]=test?((x*13+y*17)&255)|(((x*37+y*19)&255)<<8)|(((x*11+y*23)&255)<<16)|(((x*7+y*3)&255)<<24):0xFFFFFF00;
            destination[i]=test>=4?((x^y)&1?0x80503010:0xFF201060):0;
        }
        if(test>=2)for(unsigned i=0;i<4;++i){float *v=vertices+i*28;v[8]=1;v[9]=v[10]=v[11]=0;v[12]=0;v[13]=1;v[14]=v[15]=0;}
        if(test>=3 && test!=7)for(unsigned block=0;block<4;++block){
            uint16_t colors[]={0xF800,0x07E0,0x001F,0xFFFF};uint16_t a=colors[block],b=colors[3-block];
            for(unsigned i=0;i<8;++i)blocks[block*16+i]=(uint8_t)((i*2)|((15-i*2)<<4));
            blocks[block*16+8]=a;blocks[block*16+9]=a>>8;blocks[block*16+10]=b;blocks[block*16+11]=b>>8;
            for(unsigned i=0;i<4;++i)blocks[block*16+12+i]=0xE4;
        }
        REQUIRE(h2_dxt23_gxm_8x8(blocks,64,blocks,64));
        memcpy(indices,test==5?back:front,sizeof front);
        CHECK(sceGxmBeginScene(ctx,0,rt,NULL,NULL,NULL,&color,&depth));
        sceGxmSetViewportEnable(ctx,SCE_GXM_VIEWPORT_ENABLED);sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_NONE,0,0,639,479);
        sceGxmSetViewport(ctx,320,320,240,-240,0.0f,1.0f);sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
        sceGxmSetFrontFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);sceGxmSetBackFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
        sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
        sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
        sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
        sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
        sceGxmSetVertexProgram(ctx,vprog);sceGxmSetFragmentProgram(ctx,copyprog);
        CHECK(sceGxmSetVertexStream(ctx,0,vertices));
        if(test>=6){CHECK(sceGxmTextureSetMinFilter(&tex[1],SCE_GXM_TEXTURE_FILTER_POINT));CHECK(sceGxmTextureSetMagFilter(&tex[1],SCE_GXM_TEXTURE_FILTER_POINT));}
        CHECK(sceGxmSetFragmentTexture(ctx,copy_sampler,test>=6?&tex[1]:&copytex));
        CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));
        if(test<6){
        sceGxmSetFragmentProgram(ctx,fprog);
        void *fu;CHECK(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&fu));CHECK(sceGxmSetUniformDataF(fu,uniform,0,72,constants));
        CHECK(sceGxmSetVertexStream(ctx,0,vertices));
        for(unsigned i=0;i<2;++i)CHECK(sceGxmSetFragmentTexture(ctx,samplers[i],&tex[i]));
        CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));
        }
        SceGxmNotification done={sceGxmGetNotificationRegion(),test+1};REQUIRE(done.address);
        CHECK(sceGxmEndScene(ctx,NULL,&done));CHECK(sceGxmNotificationWait(&done));sceGxmFinish(ctx);
        unsigned nonblack=0;for(unsigned i=0;i<640*480;++i)nonblack+=(target[i]&0xFFFFFF)!=0;
        sceClibPrintf("[screen-probe] test=%u nonblack=%u first=%08X last=%08X\n",test,nonblack,target[0],target[640*480-1]);
        char path[128];snprintf(path,sizeof path,"ux0:data/xita-halo2/screen-probe-%u.bin",test);FILE *f=fopen(path,"wb");REQUIRE(f);REQUIRE(fwrite(target,4,640*480,f)==640*480);REQUIRE(!fclose(f));
    }
    sceClibPrintf("[screen-probe] complete; eight captured/synthetic fixtures, no guest draw or menu\n");
    sceKernelExitProcess(0);return 0;
}
