#define main feature_correctness_main
#include "feature_build_reference.c"
#undef main
#include "kernel/xk_feature_surface_admission.h"
int main(void){
 g_xram=calloc(1,0x300000);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
 for(unsigned i=0;i<0x300;i++)g_xpt[i]=i*4096;
 uint8_t *before=malloc(0x300000),*expected=malloc(0x300000);assert(before&&expected);
 reference_init_constants();uint32_t rng=12345;
 const unsigned sp=0x10000,vertex=0x21000,out=0x40000;
 for(unsigned trial=0;trial<10000;trial++){
  float points[8][3],plane[4];
  for(unsigned i=0;i<8;i++)for(unsigned j=0;j<3;j++){
   rng=rng*1664525u+1013904223u;points[i][j]=(float)((int32_t)(rng>>8)-0x800000)/4096.f;
  }
  for(unsigned i=0;i<4;i++)plane[i]=(float)((int)((trial>>(i*3))%7)-3)/4.f;
  unsigned count=trial%9,initial=trial%257;
  float height=(float)((int)(trial%31)-10)/8.f,radius=(float)(trial%29)/16.f;
  memset(g_xram+out,(uint8_t)trial,0xb000);put32(out+4,initial);
  memcpy(g_xram+vertex,points,sizeof points);memcpy(g_xram+0x24000,plane,sizeof plane);
  put32(sp,0x12345678);put32(sp+4,count);put32(sp+8,vertex);put32(sp+12,0x24000);
  put32(sp+16,bits(height));put32(sp+20,bits(radius));put32(sp+24,rng);put32(sp+28,rng^0xabcdef01);
  put32(sp+32,(uint8_t)rng);put32(sp+36,(uint8_t)(rng>>8));put32(sp+40,(uint16_t)(rng>>16));put32(sp+44,out);
  xctx c={0};for(unsigned i=0;i<8;i++){c.r[i]=rng+i*0x12345;c.st[i]=(double)i+.25;}
  c.r[4]=sp;c.preempt=10000;c.fcw=0x37f;c.fsp=trial%8;c.fsw=(trial&0x4700);
  c.f_cf_override=trial%2;c.f_of_override=(trial>>1)%2;c.f_cf=1;c.f_of=1;
  xctx original=c;memcpy(before,g_xram,0x300000);f_00085020(&original);memcpy(expected,g_xram,0x300000);memcpy(g_xram,before,0x300000);
  assert(xk_feature_try_surface_emit(&c,0x300000,1<<20));
  if(memcmp(&c,&original,sizeof c)||memcmp(g_xram,expected,0x300000)){
   fprintf(stderr,"surface-state mismatch trial=%u count=%u initial=%u height=%g plane=%g,%g,%g\n",trial,count,initial,height,plane[0],plane[1],plane[2]);
   for(unsigned i=0;i<sizeof c;i++)if(((uint8_t*)&c)[i]!=((uint8_t*)&original)[i])fprintf(stderr,"ctx byte %u got %02x expected %02x\n",i,((uint8_t*)&c)[i],((uint8_t*)&original)[i]);
   for(unsigned i=0;i<0x300000;i++)if(g_xram[i]!=expected[i]){fprintf(stderr,"memory first mismatch %x\n",i);break;}
   return 1;
  }
 }
 /* An untouched output gap may be noncontiguous; only accessed bytes matter. */
 g_xpt[(out>>12)+1]+=4096;
 put32(out+4,0);
 xctx gap={0};gap.r[4]=sp;gap.fcw=0x37f;gap.preempt=1000;
 xctx gap_expected=gap;memcpy(before,g_xram,0x300000);
 f_00085020(&gap_expected);memcpy(expected,g_xram,0x300000);
 memcpy(g_xram,before,0x300000);
 assert(xk_feature_try_surface_emit(&gap,0x300000,1<<20));
 assert(!memcmp(&gap,&gap_expected,sizeof gap)&&!memcmp(g_xram,expected,0x300000));
 g_xpt[(out>>12)+1]-=4096;
 puts("PASS untouched output gap mapping");
 /* Admission refusals must leave context and arena unchanged. */
 put32(sp+4,4);put32(sp+8,vertex);put32(sp+12,0x24000);put32(sp+44,out);
 put32(out+4,0);reference_init_constants();memcpy(expected,g_xram,0x300000);
 for(unsigned test=0;test<12;test++){
  memcpy(g_xram,expected,0x300000);for(unsigned i=0;i<0x300;i++)g_xpt[i]=i*4096;
  xctx c={0};c.r[4]=sp;c.fcw=0x37f;c.preempt=1000;uint64_t extent=0x300000;
  if(test==0)c.fcw=0x27f;
  if(test==1)c.preempt=0;
  if(test==2)put32(sp+44,sp-4);
  if(test==3)put32(sp+12,out+0x4408);
  if(test==4)put32(sp+8,sp-8);
  if(test==5)put32(0x1f0a68,1);
  if(test==6)put32(0x1eaf30,0);
  if(test==7)g_xpt[(out+0x4408)>>12]+=4096;
  if(test==8)c.r[4]=0xfffffff0;
  if(test==9)put32(sp+4,9);
  if(test==10)put32(0x24000,0x7fc00000);
  if(test==11)extent=0x1000;
  xctx unchanged=c;memcpy(before,g_xram,0x300000);
  assert(!xk_feature_try_surface_emit(&c,extent,1<<20));
  assert(!memcmp(&c,&unchanged,sizeof c)&&!memcmp(g_xram,before,0x300000));
 }
 puts("PASS surface admission: 12 unchanged-state refusals");
 free(before);free(expected);free(g_xpt);free(g_xram);
 puts("PASS surface state: 10000 full-context/full-arena comparisons");
}
