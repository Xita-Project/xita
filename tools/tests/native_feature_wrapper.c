#define main feature_correctness_main
#include "feature_build_reference.c"
#undef main
#include "feature_scope_fixture.h"
#include "kernel/xk_native_feature.c"
static unsigned owner_reason,worker_admitted,helper,serial_admitted;
int xv_object_feature_serial_admit(const xctx *c){(void)c;return serial_admitted;}
unsigned xv_object_world_run_admit(xctx *c){(void)c;return worker_admitted;}
unsigned xv_object_census_admit(const xctx *c){(void)c;return owner_reason;}
int xv_scene_thread_on_helper(void){return helper;}
uint32_t xk_mem_arena_size(void){return 0x300000;}
void xk_os_log(const char *fmt,...){(void)fmt;}
static int run(xctx *c){return xv_native_feature_vertices(c,f_000855F0,f_000862A0,f_00086170);}
int main(void){
 g_xram=calloc(1,0x300000);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
 uint8_t *before=malloc(0x300000),*expected=malloc(0x300000);assert(before&&expected);
 for(unsigned test=0;test<11;test++){
  xctx c;scope_fixture(&c);put32(0x30808,32);
  for(unsigned i=0;i<32;i++)put32(0x3080c+4*i,16000+i%2);
  helper=0;worker_admitted=0;serial_admitted=0;owner_reason=XV_LC_OK;xv_native_feature_test_mode(1);
  if(test==0)xv_native_feature_test_mode(0);
  if(test==1)helper=1;
  if(test==2)owner_reason=XV_LC_QUEUE;
  if(test==3)put32(0x30808,8);
  if(test==5)owner_reason=XV_LC_GUARD;
  if(test==6){worker_admitted=1;owner_reason=XV_LC_WORKER;}
  if(test==7){owner_reason=XV_LC_UNINITIALIZED;}
  if(test==8){owner_reason=XV_LC_UNINITIALIZED;serial_admitted=1;}
  if(test==9)put32(c.r[4]+12,0);
  if(test==10)c.fcw=0x27f;
  xctx saved=c;memcpy(before,g_xram,0x300000);
  if(test<4||test==7||test>=9){assert(!run(&c));assert(memcmp(&c,&saved,sizeof c)==0&&memcmp(g_xram,before,0x300000)==0);}
  else{
   f_000868F0(&saved);memcpy(expected,g_xram,0x300000);memcpy(g_xram,before,0x300000);
   assert(run(&c));assert(memcmp(&c,&saved,sizeof c)==0&&memcmp(g_xram,expected,0x300000)==0);
  }
 }
 assert(feature_ownership[1]==1);
 assert(feature_ownership[3+XV_LC_QUEUE]==1);
 assert(feature_ownership[3+XV_LC_UNINITIALIZED]==1);
 assert(feature_counts[1]==3);
 assert(feature_selection[2]==1&&feature_selection[3]==1&&feature_selection[4]==1);
 assert(feature_sizes[3]==1&&feature_sizes[5]==6);
 free(before);free(expected);free(g_xpt);free(g_xram);
 puts("PASS feature wrapper: disabled/helper/queue/selection refusal; owner/guarded owner/retained worker equivalence");
}
