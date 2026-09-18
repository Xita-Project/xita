/* Isolated BC1 GPU probe. Private shaders and captured inputs are
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
#ifndef H2_BC1_FLOAT_SAMPLE
#define H2_BC1_FLOAT_SAMPLE 0
#endif
#define CHECK(x) do { int err=(x); if(err<0){sceClibPrintf("[bc1-probe] FAIL %s = %08X\n",#x,err);sceKernelExitProcess(1);} }while(0)
#define REQUIRE(x) do { if(!(x)){sceClibPrintf("[bc1-probe] FAIL %s\n",#x);sceKernelExitProcess(1);} }while(0)
static void *alloc(unsigned size, int kind, unsigned *offset)
{
    size=(size+4095)&~4095u;
    int uid=sceKernelAllocMemBlock("h2_bc1_probe",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,size,NULL);
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
    const SceGxmProgram *vp=load("app0:bc1.vert.gxp",0),*fp=load("app0:bc1.frag.gxp",0);
    SceGxmShaderPatcherId vid,fid;CHECK(sceGxmShaderPatcherRegisterProgram(patcher,vp,&vid));CHECK(sceGxmShaderPatcherRegisterProgram(patcher,fp,&fid));
    const char *names[]={"IN.position","IN.blendweight","IN.normal","IN.color0","IN.color1","IN.fog","IN.psize"};
    SceGxmVertexAttribute attr[7]={{0}};
    for(unsigned i=0;i<7;++i){attr[i].offset=i*16;attr[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attr[i].componentCount=4;attr[i].regIndex=sceGxmProgramParameterGetResourceIndex(parameter(vp,names[i]));}
    SceGxmVertexStream stream={.stride=112,.indexSource=SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    SceGxmVertexProgram *vprog=NULL;SceGxmFragmentProgram *fprog=NULL;
    CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher,vid,attr,7,&stream,1,&vprog));
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&fprog));
    const SceGxmProgram *copy=load("app0:bc1.copy.frag.gxp",0);
    SceGxmShaderPatcherId copyid;SceGxmFragmentProgram *copyprog=NULL;
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher,copy,&copyid));
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,copyid,H2_BC1_FLOAT_SAMPLE?SCE_GXM_OUTPUT_REGISTER_FORMAT_HALF4:SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&copyprog));
    unsigned copy_sampler=sceGxmProgramParameterGetResourceIndex(parameter(copy,"copy_source"));REQUIRE(copy_sampler<4);
    unsigned samplers[4],used=0;
    for(unsigned i=0;i<4;++i){char name[8];snprintf(name,sizeof name,"tex%u",i);samplers[i]=sceGxmProgramParameterGetResourceIndex(parameter(fp,name));REQUIRE(samplers[i]<4 && !(used&(1u<<samplers[i])));used|=1u<<samplers[i];}
    const SceGxmProgramParameter *uniform=parameter(fp,"psc");REQUIRE(sceGxmProgramParameterGetArraySize(uniform)==18);
    float *original_constants=load("app0:bc1.constants.bin",72*4),*original_vertices=load("app0:bc1.vertices.bin",4*112);
    uint8_t *original_blocks=load("app0:bc1.texture.bin",32);
    float *vertices=alloc(4096,0,NULL);uint16_t *indices=alloc(4096,0,NULL);
    const uint16_t front[]={0,1,2,0,2,3},back[]={0,2,1,0,3,2};
    uint32_t *target=alloc(320*240*(H2_BC1_FLOAT_SAMPLE?8:4),0,NULL);uint8_t *blocks[4];SceGxmTexture tex[4];
    for(unsigned i=0;i<4;++i){blocks[i]=alloc(4096,0,NULL);CHECK(sceGxmTextureInitSwizzled(&tex[i],blocks[i],SCE_GXM_TEXTURE_FORMAT_UBC1_ABGR,8,8,1));CHECK(sceGxmTextureSetUAddrMode(&tex[i],SCE_GXM_TEXTURE_ADDR_REPEAT));CHECK(sceGxmTextureSetVAddrMode(&tex[i],SCE_GXM_TEXTURE_ADDR_REPEAT));}
    SceGxmRenderTargetParams rp={.width=320,.height=240,.multisampleMode=SCE_GXM_MULTISAMPLE_NONE,.scenesPerFrame=1,.driverMemBlock=-1};
    SceGxmRenderTarget *rt=NULL;CHECK(sceGxmCreateRenderTarget(&rp,&rt));
    SceGxmDepthStencilSurface depth;void *depth_data=alloc(320*256*4,0,NULL);
    CHECK(sceGxmDepthStencilSurfaceInit(&depth,SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,320,depth_data,NULL));
    SceGxmColorSurface color;CHECK(sceGxmColorSurfaceInit(&color,H2_BC1_FLOAT_SAMPLE?SCE_GXM_COLOR_FORMAT_F16F16F16F16_ABGR:SCE_GXM_COLOR_FORMAT_A8R8G8B8,SCE_GXM_COLOR_SURFACE_LINEAR,SCE_GXM_COLOR_SURFACE_SCALE_NONE,H2_BC1_FLOAT_SAMPLE?SCE_GXM_OUTPUT_REGISTER_SIZE_64BIT:SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,320,240,320,target));
    for(unsigned test=0;test<8;++test){
        float constants[72];memcpy(constants,original_constants,sizeof constants);memcpy(vertices,original_vertices,4*112);
        for(unsigned unit=0;unit<4;++unit){
            memcpy(blocks[unit],original_blocks,32);
            if(test!=0 && test!=5)for(unsigned block=0;block<4;++block){
                /* Both BC1 endpoint orders, all selectors, different quadrants.
                 * Test7 rotates each unit independently to catch alias/binding mistakes. */
                const uint16_t colors[]={0xF800,0x07E0,0x001F,0xFFFF};unsigned k=(block+(test==7?unit:0))&3;
                uint16_t a=colors[k],b=colors[3-k];
                blocks[unit][block*8]=a;blocks[unit][block*8+1]=a>>8;blocks[unit][block*8+2]=b;blocks[unit][block*8+3]=b>>8;
                for(unsigned j=0;j<4;++j)blocks[unit][block*8+4+j]=0xE4;
            }
            REQUIRE(h2_dxt1_gxm_8x8(blocks[unit],32,blocks[unit],32));
            CHECK(sceGxmTextureSetMinFilter(&tex[unit],test==4||test==5?SCE_GXM_TEXTURE_FILTER_POINT:SCE_GXM_TEXTURE_FILTER_LINEAR));
            CHECK(sceGxmTextureSetMagFilter(&tex[unit],test==4||test==5?SCE_GXM_TEXTURE_FILTER_POINT:SCE_GXM_TEXTURE_FILTER_LINEAR));
        }
        if(test>=2)for(unsigned vertex=0;vertex<4;++vertex)for(unsigned unit=0;unit<4;++unit){
            float *v=vertices+vertex*28+(unit+1)*4;unsigned right=vertex>=2,bottom=vertex==1||vertex==2;
            v[0]=right?2.375f+unit*.125f:-1.125f+unit*.125f;v[1]=bottom?1.75f-unit*.25f:-.75f-unit*.25f;v[2]=0;v[3]=1;
            if(test==4||test==5){v[0]=right;v[1]=bottom;}
        }
        if(test==6||test==7)for(unsigned i=0;i<72;++i)constants[i]=(float)((i*73+41)&255)/255.0f;
        memcpy(indices,test==3?back:front,sizeof front);
        float original_coords[4][4];
        for(unsigned vertex=0;vertex<4;++vertex)memcpy(original_coords[vertex],vertices+vertex*28+4,16);
        for(unsigned sample_unit=0;sample_unit<(H2_BC1_FLOAT_SAMPLE?4u:1u);++sample_unit){
        if(H2_BC1_FLOAT_SAMPLE)for(unsigned vertex=0;vertex<4;++vertex){
            const float *coord=sample_unit?vertices+vertex*28+(sample_unit+1)*4:original_coords[vertex];
            memcpy(vertices+vertex*28+4,coord,16);
        }
        CHECK(sceGxmBeginScene(ctx,0,rt,NULL,NULL,NULL,&color,&depth));
        sceGxmSetViewportEnable(ctx,SCE_GXM_VIEWPORT_ENABLED);sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_NONE,0,0,319,239);
        sceGxmSetViewport(ctx,160,160,120,-120,0.0f,1.0f);sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
        sceGxmSetFrontFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);sceGxmSetBackFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
        sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
        sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
        sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
        sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
        sceGxmSetVertexProgram(ctx,vprog);CHECK(sceGxmSetVertexStream(ctx,0,vertices));
        if(H2_BC1_FLOAT_SAMPLE||test==4||test==5){sceGxmSetFragmentProgram(ctx,copyprog);CHECK(sceGxmSetFragmentTexture(ctx,copy_sampler,&tex[sample_unit]));}
        else{sceGxmSetFragmentProgram(ctx,fprog);void *fu;CHECK(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&fu));CHECK(sceGxmSetUniformDataF(fu,uniform,0,72,constants));for(unsigned unit=0;unit<4;++unit)CHECK(sceGxmSetFragmentTexture(ctx,samplers[unit],&tex[unit]));}
        CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));
        SceGxmNotification done={sceGxmGetNotificationRegion(),test*4+sample_unit+1};REQUIRE(done.address);CHECK(sceGxmEndScene(ctx,NULL,&done));CHECK(sceGxmNotificationWait(&done));sceGxmFinish(ctx);
        unsigned nonblack=0;for(unsigned i=0;i<320*240;++i)nonblack+=(target[i]&0xFFFFFF)!=0;
        if(H2_BC1_FLOAT_SAMPLE)sceClibPrintf("[bc1-probe] test=%u sample_unit=%u RGBA16 capture; readback encoding is platform-specific\n",test,sample_unit);
        else sceClibPrintf("[bc1-probe] test=%u nonblack=%u first=%08X last=%08X\n",test,nonblack,target[0],target[320*240-1]);
        char path[128];
        if(H2_BC1_FLOAT_SAMPLE)snprintf(path,sizeof path,"ux0:data/xita-halo2/bc1-probe-sample-%u-%u.rgba16",test,sample_unit);
        else snprintf(path,sizeof path,"ux0:data/xita-halo2/bc1-probe-%u.bin",test);
        FILE *f=fopen(path,"wb");REQUIRE(f);REQUIRE(fwrite(target,H2_BC1_FLOAT_SAMPLE?8:4,320*240,f)==320*240);REQUIRE(!fclose(f));
        }
    }
    sceClibPrintf("[bc1-probe] complete; eight captured/synthetic fixtures, no guest draw or menu\n");sceKernelExitProcess(0);return 0;
}
