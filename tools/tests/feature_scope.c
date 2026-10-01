/* Focused high-index/narrow-range and failure-atomicity checks. */
#define main feature_correctness_main
#include "feature_build_reference.c"
#undef main
#include "feature_scope_fixture.h"
int main(void){
 g_xram=calloc(1,0x300000);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
 uint8_t *before=malloc(0x300000),*expected=malloc(0x300000);assert(before&&expected);
 xctx c;scope_fixture(&c);xctx native=c;
 memcpy(before,g_xram,0x300000);f_000868F0(&c);memcpy(expected,g_xram,0x300000);
 memcpy(g_xram,before,0x300000);
 assert(xk_feature_try_query_vertices(&native,0x300000,1<<20,f_000855F0,f_000862A0,f_00086170));
 assert(memcmp(&native,&c,sizeof c)==0&&memcmp(expected,g_xram,0x300000)==0);
 for(unsigned kind=0;kind<13;kind++){
  scope_fixture(&c);
  switch(kind){
   case 0:c.preempt=7;break;
   case 1:put32(0x3080c,16384);break;
   case 2:put32(0x80000+16000*16+12,16384);break;
   case 3:put32(0x100000+16000*24+16,16384);break;
   case 4:put32(0x20058,UINT32_MAX-16);break;
   case 5:memmove(g_xram+0x30800,g_xram+0x30000,0x82c);c.r[7]=0x30800;g_xpt[0x31]+=4096;break;
   case 6:g_xpt[(0x80000+16000*16)>>12]=0x40000;break;
   case 7:put32(0x20054,0);break;
   case 8:put32(0x20048,UINT32_MAX);break;
   case 9:put32(0x1f0a68,1);break;
   case 10:put32(0x80000+16000*16,0x7fc00000);break;
   case 11:g_xpt[0x41]+=4096;break;
   case 12:put32(0x40000,257);break;
  }
  xctx saved=c;memcpy(before,g_xram,0x300000);
  xk_feature_vertex_scope scope={0},untouched={0};
  if(xk_feature_prepare_vertex_scope(&c,0x300000,1<<20,f_000855F0,&scope)){fprintf(stderr,"unexpected admission kind=%u\n",kind);abort();}
  assert(memcmp(&scope,&untouched,sizeof scope)==0);
  assert(memcmp(&c,&saved,sizeof c)==0&&memcmp(before,g_xram,0x300000)==0);
 }
 free(before);free(expected);free(g_xpt);free(g_xram);
 puts("PASS scope: high-index narrow query matches original; 13 refusals preserve context, arena and scope");
}
