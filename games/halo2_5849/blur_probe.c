/* Isolated blur GPU probe. Private shaders and captured inputs are
 * supplied externally. No Xbox commands execute; fixtures are not a menu. */
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifndef H2_BLUR_FLOAT_SAMPLE
#define H2_BLUR_FLOAT_SAMPLE 0
#endif
unsigned int _newlib_heap_size_user = 4 * 1024 * 1024;
#define CHECK(x) do { int err=(x); if(err<0){sceClibPrintf("[blur-probe] FAIL %s = %08X\n",#x,err);sceKernelExitProcess(1);} }while(0)
#define REQUIRE(x) do { if(!(x)){sceClibPrintf("[blur-probe] FAIL %s\n",#x);sceKernelExitProcess(1);} }while(0)
static void *alloc(unsigned size, int kind, unsigned *offset)
{
    size=(size+4095)&~4095u;
    int uid=sceKernelAllocMemBlock("h2_blur_probe",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,size,NULL);
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
    FILE *f=fopen(path,"rb");REQUIRE(f);REQUIRE(!fseek(f,0,SEEK_END));long n=ftell(f);REQUIRE(n>0&&n<=160*120*4);
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
    const SceGxmProgram *vp=load("app0:blur.vert.gxp",0),*fp=load("app0:blur.frag.gxp",0);
    SceGxmShaderPatcherId vid,fid;CHECK(sceGxmShaderPatcherRegisterProgram(patcher,vp,&vid));CHECK(sceGxmShaderPatcherRegisterProgram(patcher,fp,&fid));
    const char *names[]={"IN.position","IN.blendweight","IN.normal","IN.color0","IN.color1","IN.fog","IN.psize"};
    SceGxmVertexAttribute attr[7]={{0}};
    for(unsigned i=0;i<7;++i){attr[i].offset=i*16;attr[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attr[i].componentCount=4;attr[i].regIndex=sceGxmProgramParameterGetResourceIndex(parameter(vp,names[i]));}
    SceGxmVertexStream stream={.stride=112,.indexSource=SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    SceGxmVertexProgram *vprog=NULL;SceGxmFragmentProgram *fprog=NULL;
    CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher,vid,attr,7,&stream,1,&vprog));
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&fprog));
    SceGxmFragmentProgram *copyprog=NULL;unsigned copy_sampler=0;
    if(H2_BLUR_FLOAT_SAMPLE){
        const SceGxmProgram *copy=load("app0:blur.copy.frag.gxp",0);SceGxmShaderPatcherId copyid;
        CHECK(sceGxmShaderPatcherRegisterProgram(patcher,copy,&copyid));
        CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,copyid,SCE_GXM_OUTPUT_REGISTER_FORMAT_HALF4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&copyprog));
        copy_sampler=sceGxmProgramParameterGetResourceIndex(parameter(copy,"copy_source"));REQUIRE(copy_sampler<4);
    }
    const SceGxmProgramParameter *uniform=parameter(fp,"psc");REQUIRE(sceGxmProgramParameterGetArraySize(uniform)==18);
    unsigned samplers[4],used=0;
    for(unsigned i=0;i<4;++i){char name[16];snprintf(name,sizeof name,"tex%u",i);samplers[i]=sceGxmProgramParameterGetResourceIndex(parameter(fp,name));REQUIRE(samplers[i]<4&&!(used&(1u<<samplers[i])));used|=1u<<samplers[i];}
    float *constants=load("app0:blur.constants.bin",72*4),*original_vertices=load("app0:blur.vertices.bin",4*112);
    uint32_t *original_texture=load("app0:blur.texture.bin",160*120*4);
    float *vertices=alloc(4096,0,NULL);uint16_t *indices=alloc(4096,0,NULL);
    const uint16_t front[]={0,1,2,0,2,3},back[]={0,2,1,0,3,2};
    uint32_t *target=alloc(160*120*(H2_BLUR_FLOAT_SAMPLE?8:4),0,NULL),*pixels[4];SceGxmTexture tex[4];
    for(unsigned i=0;i<4;++i){pixels[i]=alloc(160*120*4,0,NULL);
        CHECK(sceGxmTextureInitLinear(&tex[i],pixels[i],SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB,160,120,1));
        CHECK(sceGxmTextureSetMinFilter(&tex[i],SCE_GXM_TEXTURE_FILTER_LINEAR));CHECK(sceGxmTextureSetMagFilter(&tex[i],SCE_GXM_TEXTURE_FILTER_LINEAR));
        CHECK(sceGxmTextureSetUAddrMode(&tex[i],SCE_GXM_TEXTURE_ADDR_CLAMP));CHECK(sceGxmTextureSetVAddrMode(&tex[i],SCE_GXM_TEXTURE_ADDR_CLAMP));}
    SceGxmRenderTargetParams rp={.width=160,.height=120,.multisampleMode=SCE_GXM_MULTISAMPLE_NONE,.scenesPerFrame=1,.driverMemBlock=-1};
    SceGxmRenderTarget *rt=NULL;CHECK(sceGxmCreateRenderTarget(&rp,&rt));
    SceGxmDepthStencilSurface depth;void *depth_data=alloc(160*128*4,0,NULL);
    CHECK(sceGxmDepthStencilSurfaceInit(&depth,SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,160,depth_data,NULL));
    SceGxmColorSurface color;CHECK(sceGxmColorSurfaceInit(&color,H2_BLUR_FLOAT_SAMPLE?SCE_GXM_COLOR_FORMAT_F16F16F16F16_ABGR:SCE_GXM_COLOR_FORMAT_A8R8G8B8,SCE_GXM_COLOR_SURFACE_LINEAR,SCE_GXM_COLOR_SURFACE_SCALE_NONE,H2_BLUR_FLOAT_SAMPLE?SCE_GXM_OUTPUT_REGISTER_SIZE_64BIT:SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,160,120,160,target));
    for(unsigned test=0;test<8;++test){
        memcpy(vertices,original_vertices,4*112);memcpy(indices,test==4?back:front,sizeof front);
        float factors[72];memcpy(factors,constants,sizeof factors);
        if(test==5)for(unsigned v=0;v<4;++v)for(unsigned a=1;a<=4;++a){vertices[v*28+a*4]+=.5f;vertices[v*28+a*4+1]+=.25f;}
        if(test==6)factors[10*4+3]=.125f;
        if(test==7)factors[10*4+3]=1;
        for(unsigned u=0;u<4;++u)for(unsigned y=0;y<120;++y)for(unsigned x=0;x<160;++x){
            uint32_t word=original_texture[y*160+x];
            if(test){unsigned phase=test==1?0:u;
                unsigned r=(x*17+y*7+phase*41)&255,g=(x*5+y*23+phase*53)&255,b=(x*11+y*13+phase*67)&255,a=(x+y+phase*71)&255;
                if(test>=3&&test!=5){r=(x/4*29+y/4*17+phase*37)&255;g=(x/4*11+y/4*31+phase*43)&255;b=((x/4^y/4)&1)?255:128;a=(x*3+y*7+phase*61)&255;}
                if(test==7){r=96+u*48;g=64+u*32;b=32+u*56;a=200+u*16;}
                word=a<<24|r<<16|g<<8|b;
            }
            pixels[u][y*160+x]=word;
        }
        float original_coords[4][4];
        for(unsigned v=0;v<4;++v)memcpy(original_coords[v],vertices+v*28+4,16);
        for(unsigned sample_unit=0;sample_unit<(H2_BLUR_FLOAT_SAMPLE?4u:1u);++sample_unit){
        if(H2_BLUR_FLOAT_SAMPLE)for(unsigned v=0;v<4;++v){
            const float *coord=sample_unit?vertices+v*28+(sample_unit+1)*4:original_coords[v];
            memcpy(vertices+v*28+4,coord,16);
        }
        memset(target,0x5A,160*120*(H2_BLUR_FLOAT_SAMPLE?8:4));
        CHECK(sceGxmBeginScene(ctx,0,rt,NULL,NULL,NULL,&color,&depth));
        sceGxmSetViewportEnable(ctx,SCE_GXM_VIEWPORT_ENABLED);sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_NONE,0,0,159,119);
        sceGxmSetViewport(ctx,80,80,60,-60,0,1);sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
        sceGxmSetFrontFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);sceGxmSetBackFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
        sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
        sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
        sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
        sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
        sceGxmSetVertexProgram(ctx,vprog);CHECK(sceGxmSetVertexStream(ctx,0,vertices));
        if(H2_BLUR_FLOAT_SAMPLE){sceGxmSetFragmentProgram(ctx,copyprog);CHECK(sceGxmSetFragmentTexture(ctx,copy_sampler,&tex[sample_unit]));}
        else{sceGxmSetFragmentProgram(ctx,fprog);void *fu;CHECK(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&fu));CHECK(sceGxmSetUniformDataF(fu,uniform,0,72,factors));
            for(unsigned i=0;i<4;++i)CHECK(sceGxmSetFragmentTexture(ctx,samplers[i],&tex[i]));}
        CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));
        SceGxmNotification done={sceGxmGetNotificationRegion(),test*4+sample_unit+1};REQUIRE(done.address);CHECK(sceGxmEndScene(ctx,NULL,&done));CHECK(sceGxmNotificationWait(&done));sceGxmFinish(ctx);
        char path[128];
        if(H2_BLUR_FLOAT_SAMPLE)snprintf(path,sizeof path,"ux0:data/xita-halo2/blur-probe-sample-%u-%u.rgba16",test,sample_unit);
        else snprintf(path,sizeof path,"ux0:data/xita-halo2/blur-probe-%u.bin",test);
        FILE *f=fopen(path,"wb");REQUIRE(f);REQUIRE(fwrite(target,H2_BLUR_FLOAT_SAMPLE?8:4,160*120,f)==160*120);REQUIRE(!fclose(f));
        if(H2_BLUR_FLOAT_SAMPLE)sceClibPrintf("[blur-probe] test=%u sample_unit=%u RGBA16; explicit platform readback encoding required\n",test,sample_unit);
        else{unsigned nonblack=0;for(unsigned i=0;i<160*120;++i)nonblack+=(target[i]&0xFFFFFF)!=0;
            sceClibPrintf("[blur-probe] test=%u nonblack=%u first=%08X last=%08X\n",test,nonblack,target[0],target[160*120-1]);}
        }
    }
    sceClibPrintf("[blur-probe] complete; eight captured/synthetic fixtures, no guest draw or menu\n");
    sceKernelExitProcess(0);return 0;
}
