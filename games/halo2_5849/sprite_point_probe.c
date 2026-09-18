/* Isolated packed-color quad GPU probe. Private shaders and captured inputs are
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
unsigned int _newlib_heap_size_user = 4 * 1024 * 1024;
#ifndef H2_SPRITE_SOURCE_PROBE
#define H2_SPRITE_SOURCE_PROBE 0
#endif
#define CHECK(x) do { int err=(x); if(err<0){sceClibPrintf("[sprite-point-probe] FAIL %s = %08X\n",#x,err);sceKernelExitProcess(1);} }while(0)
#define REQUIRE(x) do { if(!(x)){sceClibPrintf("[sprite-point-probe] FAIL %s\n",#x);sceKernelExitProcess(1);} }while(0)
static void *alloc(unsigned size, int kind, unsigned *offset)
{
    size=(size+4095)&~4095u;
    int uid=sceKernelAllocMemBlock("h2_sprite_probe",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,size,NULL);
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
    const SceGxmProgram *vp=load("app0:sprite.vert.gxp",0),*fp=load("app0:sprite.frag.gxp",0);
    SceGxmShaderPatcherId vid,fid;CHECK(sceGxmShaderPatcherRegisterProgram(patcher,vp,&vid));CHECK(sceGxmShaderPatcherRegisterProgram(patcher,fp,&fid));
    const char *names[]={"IN.position","IN.blendweight","IN.normal"};
    SceGxmVertexAttribute attr[3]={{0}};
    for(unsigned i=0;i<3;++i){attr[i].offset=i*16;attr[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;attr[i].componentCount=4;attr[i].regIndex=sceGxmProgramParameterGetResourceIndex(parameter(vp,names[i]));}
    SceGxmVertexStream stream={.stride=48,.indexSource=SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    SceGxmVertexProgram *vprog=NULL;SceGxmFragmentProgram *fprog=NULL;
    CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher,vid,attr,3,&stream,1,&vprog));
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&fprog));
    const SceGxmProgram *copy=load("app0:sprite.copy.frag.gxp",0);
    SceGxmShaderPatcherId copyid;SceGxmFragmentProgram *copyprog=NULL;
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher,copy,&copyid));
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher,copyid,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,NULL,vp,&copyprog));
    unsigned copy_sampler=sceGxmProgramParameterGetResourceIndex(parameter(copy,"copy_source"));REQUIRE(copy_sampler<4);
    const SceGxmProgramParameter *uniform=parameter(fp,"psc"),*sampler=parameter(fp,"tex0");
    unsigned sampler_index=sceGxmProgramParameterGetResourceIndex(sampler);REQUIRE(sampler_index<4);
    REQUIRE(sceGxmProgramParameterGetArraySize(uniform)==18);
    float *constants=load("app0:sprite.constants.bin",72*4),*original_vertices=load("app0:sprite.vertices.bin",4*48);
    uint8_t *original_texture=load("app0:sprite.texture.bin",8192);
    const SceGxmProgramParameter *vc=parameter(vp,"c");REQUIRE(sceGxmProgramParameterGetArraySize(vc)==178);
    float *original_vc=load("app0:sprite.vertex-constants.bin",712*4);
    float *vertices=alloc(4096,0,NULL);uint16_t *indices=alloc(4096,0,NULL);
    const uint16_t front[]={0,1,2,0,2,3};
    uint8_t *texture=alloc(8192,0,NULL); uint32_t *target=alloc(640*480*4,0,NULL);
    uint32_t *destination=alloc(640*480*4,0,NULL);
    SceGxmRenderTargetParams rp={.width=640,.height=480,.multisampleMode=SCE_GXM_MULTISAMPLE_NONE,.scenesPerFrame=1,.driverMemBlock=-1};
    SceGxmRenderTarget *rt=NULL;CHECK(sceGxmCreateRenderTarget(&rp,&rt));
    SceGxmDepthStencilSurface depth;void *depth_data=alloc(640*480*4,0,NULL);
    CHECK(sceGxmDepthStencilSurfaceInit(&depth,SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
        SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,640,depth_data,NULL));
    SceGxmColorSurface color;CHECK(sceGxmColorSurfaceInit(&color,SCE_GXM_COLOR_FORMAT_A8R8G8B8,SCE_GXM_COLOR_SURFACE_LINEAR,SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,640,480,640,target));
    SceGxmTexture copytex;CHECK(sceGxmTextureInitLinear(&copytex,destination,SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB,640,480,1));
    CHECK(sceGxmTextureSetMinFilter(&copytex,SCE_GXM_TEXTURE_FILTER_POINT));CHECK(sceGxmTextureSetMagFilter(&copytex,SCE_GXM_TEXTURE_FILTER_POINT));
    CHECK(sceGxmTextureSetUAddrMode(&copytex,SCE_GXM_TEXTURE_ADDR_CLAMP));CHECK(sceGxmTextureSetVAddrMode(&copytex,SCE_GXM_TEXTURE_ADDR_CLAMP));
    for(unsigned test=0;test<6;++test){
        const unsigned widths[]={1024,1024,8,8,1024,1},heights[]={8,8,1,1024,4,1};
        unsigned width=widths[test],height=heights[test];
        unsigned blocks=((width+3)/4)*((height+3)/4),bytes=blocks*16;
        uint8_t rows[8192];
        if(test==0)memcpy(rows,original_texture,8192);
        else for(unsigned b=0;b<blocks;++b){
            uint64_t alpha=0;uint32_t selectors=0;
            for(unsigned i=0;i<16;++i){alpha|=(uint64_t)((b*3+i)%16)<<(i*4);selectors|=((i+b)%4)<<(i*2);}
            uint16_t endpoints[2]={(uint16_t)(b*197+0xF800),(uint16_t)(b*331+0x07E0)};
            memcpy(rows+b*16,&alpha,8);memcpy(rows+b*16+8,endpoints,4);memcpy(rows+b*16+12,&selectors,4);
        }
        REQUIRE(h2_dxt23_gxm_rect(rows,bytes,texture,bytes,width,height));
        SceGxmTexture tex;CHECK(sceGxmTextureInitSwizzled(&tex,texture,SCE_GXM_TEXTURE_FORMAT_UBC2_ABGR,width,height,1));
        CHECK(sceGxmTextureSetMinFilter(&tex,SCE_GXM_TEXTURE_FILTER_POINT));CHECK(sceGxmTextureSetMagFilter(&tex,SCE_GXM_TEXTURE_FILTER_POINT));
        CHECK(sceGxmTextureSetUAddrMode(&tex,SCE_GXM_TEXTURE_ADDR_CLAMP));CHECK(sceGxmTextureSetVAddrMode(&tex,SCE_GXM_TEXTURE_ADDR_CLAMP));
        uint32_t *complete=malloc(width*height*4);REQUIRE(complete);
        for(unsigned tile_y=0;tile_y<height;tile_y+=480)for(unsigned tile_x=0;tile_x<width;tile_x+=640){
        memcpy(indices,front,sizeof front);memcpy(vertices,original_vertices,4*48);
        for(unsigned v=0;v<4;++v){float *a=vertices+v*12;
            a[0]=v==1||v==2?640:0;a[1]=v>=2?480:0;
            a[4]=(tile_x+(v==1||v==2?640.0f:0.0f))/width;a[5]=(tile_y+(v>=2?480.0f:0.0f))/height;
            a[8]=a[9]=a[10]=a[11]=1;
        }
        float factors[72];memcpy(factors,constants,sizeof factors);factors[0]=factors[1]=factors[2]=factors[3]=1;
        CHECK(sceGxmBeginScene(ctx,0,rt,NULL,NULL,NULL,&color,&depth));
        sceGxmSetViewportEnable(ctx,SCE_GXM_VIEWPORT_ENABLED);sceGxmSetRegionClip(ctx,SCE_GXM_REGION_CLIP_NONE,0,0,639,479);
        sceGxmSetViewport(ctx,320,320,240,-240,0.0f,1.0f);sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
        sceGxmSetFrontFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);sceGxmSetBackFragmentProgramEnable(ctx,SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
        sceGxmSetFrontStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
        sceGxmSetBackStencilFunc(ctx,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xFF,0);
        sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
        sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
        sceGxmSetVertexProgram(ctx,vprog);sceGxmSetFragmentProgram(ctx,fprog);
        void *vu,*fu;CHECK(sceGxmReserveVertexDefaultUniformBuffer(ctx,&vu));CHECK(sceGxmSetUniformDataF(vu,vc,0,712,original_vc));
        CHECK(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&fu));CHECK(sceGxmSetUniformDataF(fu,uniform,0,72,factors));
        CHECK(sceGxmSetVertexStream(ctx,0,vertices));CHECK(sceGxmSetFragmentTexture(ctx,sampler_index,&tex));
        CHECK(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,6));
        SceGxmNotification done={sceGxmGetNotificationRegion(),1+test*6+tile_y/480*2+tile_x/640};REQUIRE(done.address);
        CHECK(sceGxmEndScene(ctx,NULL,&done));CHECK(sceGxmNotificationWait(&done));sceGxmFinish(ctx);
        unsigned columns=width-tile_x<640?width-tile_x:640,rows_left=height-tile_y<480?height-tile_y:480;
        for(unsigned row=0;row<rows_left;++row)memcpy(complete+(tile_y+row)*width+tile_x,target+row*640,columns*4);
        }
        char path[128];snprintf(path,sizeof path,"ux0:data/xita-halo2/sprite-point-%u.bin",test);
        FILE *f=fopen(path,"wb");REQUIRE(f);REQUIRE(fwrite(complete,4,width*height,f)==width*height);REQUIRE(!fclose(f));free(complete);
        sceClibPrintf("[sprite-point-probe] test=%u size=%ux%u complete\n",test,width,height);
    }
    sceClibPrintf("[sprite-point-probe] complete; six complete decoded-texture captures, no guest draw or menu\n");
    sceKernelExitProcess(0);return 0;
}
