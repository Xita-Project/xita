/* Exercise the actual table, queue consumer and render admission together. */
#include "kernel/xk_occlusion.c"
#include <assert.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
static unsigned draws,build_frame=100;
static const float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void xk_os_log(const char *fmt,...){(void)fmt;}
void f_0005B4A0(xctx *c){draws++;c->r[4]+=4;}
unsigned xv_d3d_occl_begin(uint32_t h,unsigned f){(void)h;(void)f;return 1;}
void xv_d3d_occl_end(void){}
const float *xv_d3d_occl_matrix(void){return matrix;}
int xv_d3d_occl_proxy(unsigned s,float a,float b,float d,float e,float f){(void)s;(void)a;(void)b;(void)d;(void)e;(void)f;return 1;}
uint32_t xv_d3d_occl_build_frame(void){return build_frame;}
static void put(unsigned a,uint32_t v){memcpy(X_G(a),&v,4);}
static void flt(unsigned a,float v){memcpy(X_G(a),&v,4);}
int main(void){
 g_xram=calloc(1,4<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
 for(unsigned i=0;i<1024;i++)g_xpt[i]=i*4096;
 const uint32_t h=0x10001u;unsigned t=hslot(h);
 put(0x2fc6ac,0x8000);put(0x8034,0x9000);put(0x900c,1);put(0x9014,0xa000);
 put(0xb000,h);flt(0xa058,.5f);flt(0xa05c,.1f);
 xctx c={0};c.r[7]=0xb000;c.r[4]=0x20000;mode=2;
 xv_occl_result(h,99,0,0,5);
 assert(tab[t].handle==0); /* pump publication does not touch the table */
 xv_occl_render(&c);assert(draws==0&&n_skip==1&&tab[t].frame==99);
 xv_occl_result(h,100,1,0,5);build_frame=101;
 xv_occl_render(&c);assert(draws==1&&tab[t].visible); /* own samples win */
 xv_occl_result(h,101,0,0,1);build_frame=102;
 xv_occl_render(&c);assert(draws==2&&tab[t].visible); /* missing proxy is visible */
 uint32_t collision=h+1;while(hslot(collision)!=t)collision++;
 xv_occl_result(collision,102,0,0,5);consume_results();assert(tab[t].handle==collision);
 xv_occl_render(&c);assert(draws==3); /* collision cannot hide another handle */
 xv_occl_result(h,UINT32_MAX,0,0,5);consume_results(); /* new handle starts at wrap */
 build_frame=1;xv_occl_render(&c);assert(draws==3);
 build_frame=20;xv_occl_render(&c);assert(draws==4); /* stale result falls back */
 for(unsigned i=0;i<XV_OCCL_RESULT_CAP+1;i++)xv_occl_result(h,21+i,0,0,5);
 build_frame=1045;xv_occl_render(&c);assert(!results_reliable&&draws==5);
 assert(c.r[4]==0x20000+7*4);
 puts("PASS pump/table separation, hidden/visible, own-sample precedence, missing proxy, collision, wrap, stale, overflow fail-open");
 free(g_xram);free(g_xpt);
}
