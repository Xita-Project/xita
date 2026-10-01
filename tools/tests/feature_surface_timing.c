/* Warm-cache emitter timing: raw and layout-validated modes; both exclude
 * live ownership checks and do not predict Vita frame rates. */
#define main feature_correctness_main
#include "feature_build_reference.c"
#undef main
#include "kernel/xk_feature_surface_admission.h"
static volatile unsigned timing_sink;
static int order_double(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
int main(void){
 g_xram=calloc(1,0x300000);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
 for(unsigned i=0;i<0x300;i++)g_xpt[i]=i*4096;
 reference_init_constants();
 const unsigned sp=0x10000,vertex=0x21000,out=0x40000,plane_address=0x24000;
 float points[8][3];for(unsigned i=0;i<8;i++)for(unsigned j=0;j<3;j++)points[i][j]=(float)(i*3+j)/7;
 memcpy(g_xram+vertex,points,sizeof points);
 unsigned counts[]={0,3,4,8};
 uint8_t *before=malloc(0x300000),*expected=malloc(0x300000);assert(before&&expected);
 for(unsigned axis=0;axis<3;axis++)for(unsigned extrusion=0;extrusion<2;extrusion++)for(unsigned k=0;k<4;k++){
  float plane[4]={-.25f,-.25f,-.25f,2};plane[axis]=-1;memcpy(g_xram+plane_address,plane,16);
  put32(sp,0x12345678);put32(sp+4,counts[k]);put32(sp+8,vertex);put32(sp+12,plane_address);
  put32(sp+16,bits(extrusion?2.f:0.f));put32(sp+20,bits(.5f));put32(sp+24,0xffffffff);put32(sp+28,8);
  put32(sp+32,3);put32(sp+36,5);put32(sp+40,7);put32(sp+44,out);put32(out+4,0);
  xctx initial={0};initial.r[4]=sp;initial.preempt=10000;initial.fcw=0x37f;
  memcpy(before,g_xram,0x300000);xctx original=initial;f_00085020(&original);memcpy(expected,g_xram,0x300000);
  memcpy(g_xram,before,0x300000);xctx native=initial;assert(xk_feature_surface_emit_state(&native,g_xram+out));
  assert(!memcmp(&native,&original,sizeof native)&&!memcmp(g_xram,expected,0x300000));
  memcpy(g_xram,before,0x300000);
  double samples[3][7];
  for(unsigned round=0;round<8;round++)for(unsigned step=0;step<3;step++){
   unsigned mode=(round+step)%3;uint64_t start=ns_now();
   for(unsigned i=0;i<20000;i++){
    put32(out+4,0);xctx c=initial;
    if(mode==2)assert(xk_feature_try_surface_emit(&c,0x300000,1<<20));
    else if(mode==1)assert(xk_feature_surface_emit_state(&c,g_xram+out));else f_00085020(&c);
    timing_sink=c.r[0]^get32(out+4);
   }
   if(round)samples[mode][round-1]=(double)(ns_now()-start)/20000;
  }
  for(unsigned mode=0;mode<3;mode++)qsort(samples[mode],7,sizeof(double),order_double);
  printf("axis=%u extrusion=%u points=%u original_ns=%.1f admitted_ns=%.1f ratio=%.3f validated_ns=%.1f\n",axis,extrusion,counts[k],samples[0][3],samples[1][3],samples[1][3]/samples[0][3],samples[2][3]);fflush(stdout);
 }
 free(before);free(expected);free(g_xpt);free(g_xram);return 0;
}
