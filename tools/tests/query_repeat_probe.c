#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>
#include "../../recomp/kernel/xk_query_repeat_probe.c"
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;uint32_t xv_trash_off;
int xv_watch_n,xv_trace_funcs;
static unsigned admitted=1,checks,logs;
unsigned xv_object_world_run_admit(xctx *c){(void)c;return admitted;}
unsigned xk_mem_arena_size(void){return 0x210000u;}
void xv_object_math_report_check(void){checks++;}
void xk_os_log(const char *format,...){assert(strstr(format,"[query-repeat]"));logs++;}
int main(void)
{
 g_xram=calloc(1,xk_mem_arena_size());g_img_base=g_xram;
 g_xpt=malloc((1u<<20)*sizeof *g_xpt);xv_trash_off=0x20f000;
 for(unsigned i=0;i<(1u<<20);i++)g_xpt[i]=i<0x20f?i*4096:xv_trash_off;
 unsigned char data[32],copy[32];for(unsigned i=0;i<32;i++)data[i]=i;
 memcpy(g_xram+0xff1,data,15);memcpy(g_xram+0x3000,data+15,17);g_xpt[1]=0x3000;
 assert(probe_copy(0xff1,copy,32)&&!memcmp(copy,data,32));
 g_xpt[1]=xv_trash_off;assert(!probe_copy(0xff1,copy,32));
 assert(!probe_copy(0xfffffff0u,copy,32));assert(!probe_copy(0,copy,0));
 g_xpt[1]=0x1000;g_xpt[2]=0x20f800;assert(!probe_copy(0x2ff0,copy,32));g_xpt[2]=0x2000;
 assert(!probe_copy(xv_trash_off-8,copy,32));assert(!probe_copy(UINT32_MAX,copy,32));
 g_xpt[2]=xv_trash_off+0x800;assert(!probe_copy(0x2000,copy,4));g_xpt[2]=0x2000;
 xctx c={0};c.r[4]=0x4000;c.r[0]=0x5000;c.r[1]=256;c.r[2]=0x6ff1;c.fcw=0x37f;
 uint32_t args[]={0x8000,0x3f800000};memcpy(g_xram+0x4004,args,8);
 memcpy(g_xram+0x6ff1,data,32);xctx before=c;
 fenv_t env1,env2;fegetenv(&env1);
 unsigned char *arena=malloc(xk_mem_arena_size());memcpy(arena,g_xram,xk_mem_arena_size());
 admitted=0;xv_query_repeat_probe_observe(&c);assert(!ready);
 admitted=1;xv_query_repeat_probe_observe(&c);assert(probe.counts.calls==1&&probe.counts.filter_valid==1);
 assert(!memcmp(&c,&before,sizeof c)&&!memcmp(arena,g_xram,xk_mem_arena_size()));
 fegetenv(&env2);assert(!memcmp(&env1,&env2,sizeof env1));
 xv_query_repeat_probe_observe(&c);assert(probe.counts.filter.within_epoch==1);
 xv_query_repeat_probe_epoch();xv_query_repeat_probe_observe(&c);assert(probe.counts.filter.prior_epoch_only==1);
 g_xpt[0x1f0]=0xa000;uint32_t remapped_zero=0x12345678;
 memcpy(g_xram+0xaa68,&remapped_zero,4);xv_query_repeat_probe_observe(&c);
 assert(probe.entries[(probe.next+63)%64].key.zero==remapped_zero);g_xpt[0x1f0]=0x1f0000;
 g_xram[0x7000]^=1;xv_query_repeat_probe_observe(&c);assert(probe.counts.filter.misses==3);
 g_xpt[7]=xv_trash_off;xv_query_repeat_probe_observe(&c);assert(probe.counts.filter_unavailable==1);
 xv_watch_n=1;xv_query_repeat_probe_observe(&c);assert(probe.counts.invalid==1);xv_watch_n=0;
 c.r[4]=UINT32_MAX;xv_query_repeat_probe_observe(&c);assert(probe.counts.invalid==2);c=before;
 xv_query_repeat_probe_report(0);assert(!logs);xv_query_repeat_probe_report(60);assert(logs==1&&!probe.counts.calls);
 free(arena);free(g_xram);free(g_xpt);return 0;
}
