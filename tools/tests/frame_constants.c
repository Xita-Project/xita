#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define XV_CONST_POOL (256u * 1024)
#define XV_NUM_LISTS 3u
#define XV_MAX_VS 96u
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE 1
#define SCE_GXM_MEMORY_ATTRIB_READ 1
#define SCE_GXM_PARAMETER_CATEGORY_UNIFORM 1
#define SCE_GXM_PARAMETER_TYPE_F32 0
#define XV_LOG(...) log_ignore(__VA_ARGS__)
static void log_ignore(const char *fmt, ...) { (void)fmt; }
typedef int SceUID;
typedef int SceGxmContext;
typedef struct { unsigned bytes; } SceGxmProgram;
typedef struct { unsigned category,type,components,count,index,container; } SceGxmProgramParameter;
typedef struct { unsigned c_count; const char *gxp; } desc_t;
typedef struct { const desc_t *desc; SceGxmProgram *prog; SceGxmProgramParameter *p_c; } xv_vshader_t;
typedef struct { unsigned vs,const_off,const_n; } cmd_t;
typedef struct { float consts[XV_CONST_POOL]; unsigned nconsts; } cmdlist_t;
static void *blocks[64]; static unsigned next_uid,allocs,frees,maps,barriers,reserves,sets,uploads;
static int fail_alloc,fail_base,fail_map,fail_set,fail_reserve,fail_upload;
static const void *bound; static float ring[1024];
static SceUID sceKernelAllocMemBlock(const char*n,int type,unsigned bytes,void*unused) {
 (void)n;(void)unused; assert(type==1); if(fail_alloc)return -11;
 assert(next_uid+1<64);blocks[++next_uid]=malloc(bytes);assert(blocks[next_uid]);allocs++;return next_uid;
}
static int sceKernelGetMemBlockBase(SceUID id,void**p) { if(fail_base)return -12;*p=blocks[id];return 0; }
static int sceKernelFreeMemBlock(SceUID id) { free(blocks[id]);blocks[id]=NULL;frees++;return 0; }
static int sceGxmMapMemory(void*p,unsigned n,int attr) { assert(p&&n&&attr==1);if(fail_map)return -13;maps++;return 0; }
static int sceGxmUnmapMemory(void*p) { assert(p);return 0; }
static void xv_gpu_flush_pump(const void*p,unsigned n) { assert(p&&n);barriers++; }
static int sceGxmSetVertexDefaultUniformBuffer(SceGxmContext*c,const void*p) { (void)c;sets++;if(fail_set)return -14;bound=p;return 0; }
static int sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext*c,void**p) { (void)c;reserves++;if(fail_reserve)return -15;*p=ring;return 0; }
static int sceGxmSetUniformDataF(void*p,const SceGxmProgramParameter*q,unsigned off,unsigned n,const float*src) { (void)q;assert(!off&&n<=1024);uploads++;if(fail_upload)return -16;memcpy(p,src,n*4);return 0; }
#define REFLECT(name,field) static unsigned name(const SceGxmProgramParameter*p){return p->field;}
REFLECT(sceGxmProgramParameterGetCategory,category)
REFLECT(sceGxmProgramParameterGetType,type)
REFLECT(sceGxmProgramParameterGetComponentCount,components)
REFLECT(sceGxmProgramParameterGetArraySize,count)
REFLECT(sceGxmProgramParameterGetResourceIndex,index)
REFLECT(sceGxmProgramParameterGetContainerIndex,container)
static unsigned sceGxmProgramGetDefaultUniformBufferSize(const SceGxmProgram*p) {return p->bytes;}
#include "frame_constants.inc"
static uint32_t rnd=1;
static uint32_t next_random(void) {rnd=rnd*1664525+1013904223;return rnd;}
int main(int argc,char**argv) {
 (void)argv;static cmdlist_t l;SceGxmContext ctx=0;
 SceGxmProgram p={3072};SceGxmProgramParameter pc={1,0,4,192,0,14};desc_t d={192,"test"};
 xv_vshader_t vs={&d,&p,&pc};cmd_t cmd={0,0,192};l.nconsts=768;
 for(unsigned i=0;i<768;i++)l.consts[i]=(float)i;
 if(argc>1) {
  setenv("XV_FRAME_CONSTANTS","0",1);assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,0));
  assert(reserves==1&&uploads==1&&!allocs&&!sets&&!memcmp(ring,l.consts,3072));puts("disabled ring path passed");return 0;
 }
 assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,0));assert(!reserves&&!uploads&&allocs==1&&sets==1&&barriers==2);
 assert(!memcmp(bound,l.consts,3072));const void *first=bound;
 unsigned b=barriers;for(unsigned i=0;i<600;i++)assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,0));assert(barriers==b&&allocs==1);
 /* Different slots cannot modify an in-flight earlier slot. Replay ranges and
  * UI boundaries in the same frame do not recopy any source bytes. */
 l.consts[0]=9999;assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,1));assert(((const float*)first)[0]==0);
 assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,2));assert(allocs==3);
 assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,0));assert(((const float*)bound)[0]==0);
 /* Slot acquisition is performed by the existing tested fence path. */
 g_frame_constants[0].ready=0;assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,3));assert(((const float*)bound)[0]==9999);
 /* Random captured windows, shared snapshots, complete pool, float bit patterns.
  * Compare every byte including NaN payloads, not just floating values. */
 for(unsigned frame=4;frame<404;frame++) {
  for(unsigned i=0;i<XV_CONST_POOL;i++){uint32_t x=next_random();memcpy(&l.consts[i],&x,4);}
  l.nconsts=XV_CONST_POOL;g_frame_constants[frame%3].ready=0;
  for(unsigned j=0;j<31;j++) {
   cmd.const_off=(next_random()%((XV_CONST_POOL/4)-192));
   assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,frame));
   assert(!memcmp(bound,l.consts+cmd.const_off*4,3072));
  }
  cmd.const_off=XV_CONST_POOL/4-192;assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,frame));
  assert(!memcmp(bound,l.consts+cmd.const_off*4,3072));
  const uint8_t *tail=g_frame_constants[frame%3].memory+XV_FRAME_CONSTANT_BYTES;
  for(unsigned i=0;i<XV_FRAME_CONSTANT_ALLOC-XV_FRAME_CONSTANT_BYTES;i++)assert(!tail[i]);
 }
 unsigned before=sets;cmd.const_n=0;assert(!bind_vertex_constants(&ctx,&l,&cmd,&vs,404));
 cmd.const_n=192;cmd.const_off=UINT32_MAX;assert(!bind_vertex_constants(&ctx,&l,&cmd,&vs,404));
 cmd.const_off=XV_CONST_POOL/4-191;assert(!bind_vertex_constants(&ctx,&l,&cmd,&vs,404));
 cmd.const_off=0;l.nconsts=UINT32_MAX;assert(!bind_vertex_constants(&ctx,&l,&cmd,&vs,404));
 l.nconsts=768;cmd.vs=XV_MAX_VS;assert(!bind_vertex_constants(&ctx,&l,&cmd,&vs,404));cmd.vs=0;
 assert(sets==before);vs.p_c=NULL;assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,404));vs.p_c=&pc;
 /* Reflection must reject packed, shifted, truncated or non-default layouts. */
 unsigned *fields[]={&pc.category,&pc.type,&pc.components,&pc.count,&pc.index,&pc.container,&p.bytes,&d.c_count};
 for(unsigned i=0;i<sizeof fields/sizeof fields[0];i++) {
  unsigned orig=*fields[i];*fields[i]=orig+1;g_frame_constant_layout[0]=0;
  assert(!frame_constants_raw_layout(0,&vs));*fields[i]=orig;
 }
 g_frame_constant_layout[0]=-1;assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,404));assert(reserves==1&&uploads==1);
 fail_reserve=1;assert(!bind_vertex_constants(&ctx,&l,&cmd,&vs,404));fail_reserve=0;
 fail_upload=1;assert(!bind_vertex_constants(&ctx,&l,&cmd,&vs,404));fail_upload=0;
 g_frame_constant_layout[0]=0;fail_set=1;assert(!bind_vertex_constants(&ctx,&l,&cmd,&vs,404));fail_set=0;
 frame_constants_shutdown();assert(frees==allocs);
 int *failures[]={&fail_alloc,&fail_base,&fail_map};
 for(unsigned i=0;i<3;i++) {
  *failures[i]=1;assert(!bind_vertex_constants(&ctx,&l,&cmd,&vs,405+i));*failures[i]=0;
  assert(!g_frame_constants[(405+i)%3].memory);assert(allocs==frees);
 }
 assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,UINT32_MAX));assert(bind_vertex_constants(&ctx,&l,&cmd,&vs,0));
 frame_constants_shutdown();assert(frees==allocs);puts("frame constants: immutable slots, 12,800 window comparisons, layout and failure checks passed");
}
