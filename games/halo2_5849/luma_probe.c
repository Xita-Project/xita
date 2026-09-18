/* Isolated luma GPU probe. Private shaders and captured inputs are
 * supplied externally. No Xbox commands execute; fixtures are not a menu. */
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
unsigned int _newlib_heap_size_user = 4 * 1024 * 1024;
#define CHECK(x) do { int err=(x); if(err<0){sceClibPrintf("[luma-probe] FAIL %s = %08X\n",#x,err);sceKernelExitProcess(1);} }while(0)
#define REQUIRE(x) do { if(!(x)){sceClibPrintf("[luma-probe] FAIL %s\n",#x);sceKernelExitProcess(1);} }while(0)
static void *alloc(unsigned size, int kind, unsigned *offset)
{
    size=(size+4095)&~4095u;
    int uid=sceKernelAllocMemBlock("h2_luma_probe",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,size,NULL);
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
    FILE *f=fopen(path,"rb");REQUIRE(f);REQUIRE(!fseek(f,0,SEEK_END));long n=ftell(f);REQUIRE(n>0&&n<=640*480*4);
    REQUIRE(!expected||(unsigned)n==expected);rewind(f);void *p=malloc(n);REQUIRE(p);REQUIRE(fread(p,1,n,f)==(unsigned)n);REQUIRE(!fclose(f));return p;
}
static const SceGxmProgramParameter *parameter(const SceGxmProgram *p,const char *name)
{
    const SceGxmProgramParameter *v=sceGxmProgramFindParameterByName(p,name);
    if(!v)sceClibPrintf("[luma-probe] missing parameter %s\n",name);
    REQUIRE(v);return v;
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
    const SceGxmProgram *vp=load("app0:luma.vert.gxp",0),*fp=load("app0:luma.frag.gxp",0);
    SceGxmShaderPatcherId vid,fid;CHECK(sceGxmShaderPatcherRegisterProgram(patcher,vp,&vid));CHECK(sceGxmShaderPatcherRegisterProgram(patcher,fp,&fid));
    const char *names[]={"IN.position","IN.blendweight","IN.normal","IN.color0","IN.color1","IN.fog","IN.psize","IN.backcolor0"};
    SceGxmVertexAttribute attr[8]={{0}};
    for(unsigned i=0;i<8;++i){attr[i].offset=i*16;attr[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attr[i].componentCount=4;attr[i].regIndex=sceGxmProgramParameterGetResourceIndex(parameter(vp,names[i]));}
    SceGxmVertexStream stream={.stride=128,.indexSource=SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    SceGxmVertexProgram *vprog=NULL;SceGxmFragmentProgram *fprog=NULL;
    CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher,vid,attr,8,&stream,1,&vprog));
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&fprog));
    const SceGxmProgramParameter *uniform=parameter(fp,"psc"),*sampler=parameter(fp,"tex0");
    unsigned sampler_index=sceGxmProgramParameterGetResourceIndex(sampler);REQUIRE(sampler_index<4);
    REQUIRE(sceGxmProgramParameterGetArraySize(uniform)==18);
    float *constants=load("app0:luma.constants.bin",72*4),*original_vertices=load("app0:luma.vertices.bin",4*128);
    const SceGxmProgramParameter *vc=parameter(vp,"c");REQUIRE(sceGxmProgramParameterGetArraySize(vc)==8);
    float *original_vc=load("app0:luma.vertex-constants.bin",32*4);
    uint32_t *original_texture=load("app0:luma.texture.bin",640*480*4);
    float *vertices=alloc(4096,0,NULL);uint16_t *indices=alloc(4096,0,NULL);
    const uint16_t front[]={0,1,2,3},back[]={0,2,1,3};
    uint32_t *texture=alloc(640*480*4,0,NULL),*target=alloc(640*480*4,0,NULL);
    SceGxmRenderTargetParams rp={.width=640,.height=480,.multisampleMode=SCE_GXM_MULTISAMPLE_NONE,.scenesPerFrame=1,.driverMemBlock=-1};
    SceGxmRenderTarget *rt=NULL;CHECK(sceGxmCreateRenderTarget(&rp,&rt));
    SceGxmDepthStencilSurface depth;void *depth_data=alloc(640*480*4,0,NULL);
    CHECK(sceGxmDepthStencilSurfaceInit(&depth,SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
        SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,640,depth_data,NULL));
    SceGxmColorSurface color;CHECK(sceGxmColorSurfaceInit(&color,SCE_GXM_COLOR_FORMAT_A8R8G8B8,SCE_GXM_COLOR_SURFACE_LINEAR,SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,640,480,640,target));
    SceGxmTexture tex;CHECK(sceGxmTextureInitLinear(&tex,texture,SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB,640,480,1));
    CHECK(sceGxmTextureSetMinFilter(&tex,SCE_GXM_TEXTURE_FILTER_LINEAR));CHECK(sceGxmTextureSetMagFilter(&tex,SCE_GXM_TEXTURE_FILTER_LINEAR));
    CHECK(sceGxmTextureSetUAddrMode(&tex,SCE_GXM_TEXTURE_ADDR_CLAMP));CHECK(sceGxmTextureSetVAddrMode(&tex,SCE_GXM_TEXTURE_ADDR_CLAMP));
    for(unsigned test=0;test<10;++test){
        float factors[72],vconstants[32];memcpy(factors,constants,sizeof factors);memcpy(vconstants,original_vc,sizeof vconstants);
        if(test==4){vconstants[3]+=.5f;vconstants[7]+=.25f;}
        if(test==5){vconstants[0]=0;vconstants[1]=640.0f/480.0f;vconstants[4]=480.0f/640.0f;vconstants[5]=0;}
        if(test==6){vconstants[0]=vconstants[5]=.5f;vconstants[3]=100;vconstants[7]=80;factors[0]=.2f;factors[1]=.4f;factors[2]=.6f;}
        if(test==7)factors[0]=factors[1]=factors[2]=0;
        for(unsigned i=0;i<640*480;++i){unsigned x=i%640,y=i/640;
            unsigned r=(x*17+y*7)&255,g=(x*5+y*23)&255,b=(x*11+y*13)&255,a=(x*3+y*7)&255;
            if(test>=2){r=(x/4*29+y/4*17)&255;g=(x/4*11+y/4*31)&255;b=((x/4)^(y/4))&1?255:128;}
            if(test==8)r=g=b=255;
            if(test==9)a=255-a;
            texture[i]=test?(b|(g<<8)|(r<<16)|(a<<24)):original_texture[i];
        }
        memcpy(indices,test==3?back:front,sizeof front);memcpy(vertices,original_vertices,4*128);
        CHECK(sceGxmBeginScene(ctx,0,rt,NULL,NULL,NULL,&color,&depth));
        sceGxmSetViewportEnable(ctx,SCE_GXM_VIEWPORT_ENABLED);sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_NONE,0,0,639,479);
        sceGxmSetViewport(ctx,320,320,240,-240,0.0f,1.0f);sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
        sceGxmSetFrontFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);sceGxmSetBackFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
        sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
        sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
        sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
        sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
        sceGxmSetVertexProgram(ctx,vprog);sceGxmSetFragmentProgram(ctx,fprog);
        void *vu,*fu;CHECK(sceGxmReserveVertexDefaultUniformBuffer(ctx,&vu));CHECK(sceGxmSetUniformDataF(vu,vc,0,32,vconstants));
        CHECK(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&fu));CHECK(sceGxmSetUniformDataF(fu,uniform,0,72,factors));
        CHECK(sceGxmSetVertexStream(ctx,0,vertices));CHECK(sceGxmSetFragmentTexture(ctx,sampler_index,&tex));
        CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLE_STRIP,SCE_GXM_INDEX_FORMAT_U16,indices,4));
        SceGxmNotification done={sceGxmGetNotificationRegion(),test+1};REQUIRE(done.address);
        CHECK(sceGxmEndScene(ctx,NULL,&done));CHECK(sceGxmNotificationWait(&done));sceGxmFinish(ctx);
        unsigned nonblack=0;for(unsigned i=0;i<640*480;++i)nonblack+=(target[i]&0xFFFFFF)!=0;
        sceClibPrintf("[luma-probe] test=%u nonblack=%u first=%08X last=%08X\n",test,nonblack,target[0],target[640*480-1]);
        char path[128];snprintf(path,sizeof path,"ux0:data/xita-halo2/luma-probe-%u.bin",test);FILE *f=fopen(path,"wb");REQUIRE(f);REQUIRE(fwrite(target,4,640*480,f)==640*480);REQUIRE(!fclose(f));
    }
    sceClibPrintf("[luma-probe] complete; ten captured/synthetic fixtures, no guest draw or menu\n");
    sceKernelExitProcess(0);return 0;
}
