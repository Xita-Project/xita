#define main feature_correctness_main
#include "feature_build_reference.c"
#undef main
#include "kernel/xk_feature_walk_state.h"
int main(void){
 g_xram=calloc(1,0x300000);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
 for(unsigned i=0;i<0x300;i++)g_xpt[i]=i*4096;
 const unsigned sp=0x10000,geom=0x20000,vertices=0x21000,edges=0x22000,surfaces=0x23000,out=0x40000;
 uint8_t *before=malloc(0x300000),*expected=malloc(0x300000);assert(before&&expected);
 put32(geom+0x40,surfaces);put32(geom+0x4c,edges);put32(geom+0x58,vertices);
 uint32_t rng=456789;
 for(unsigned trial=0;trial<1000;trial++){
  unsigned n=1+trial%8;
  for(unsigned i=0;i<n;i++){
   unsigned side=(trial>>i)&1;
   put32(edges+i*24+16,side?1:0);put32(edges+i*24+20,side?0:1);
   put32(edges+i*24+side*4,i);put32(edges+i*24+8+side*4,(i+1)%n);
   for(unsigned j=0;j<3;j++){rng=rng*1664525u+1013904223u;put32(vertices+i*16+j*4,rng);}
  }
  put32(surfaces+4,trial%n);put32(sp,0x12345678);put32(sp+4,0);put32(sp+8,out);
  xctx c={0};for(unsigned i=0;i<8;i++){c.r[i]=rng+i;c.st[i]=(double)i+.5;}
  c.r[4]=sp;c.r[7]=geom;c.preempt=1000;c.f_cf_override=trial&1;c.f_of_override=(trial>>1)&1;c.f_cf=1;c.f_of=1;
  c.f_kind=XK_SUB;c.f_bits=32;c.f_op1=rng;c.f_op2=trial;c.f_res=rng-trial;
  xctx original=c;memcpy(before,g_xram,0x300000);f_00086A40(&original);memcpy(expected,g_xram,0x300000);memcpy(g_xram,before,0x300000);
  xk_feature_geometry g={g_xram+vertices,g_xram+edges,g_xram+surfaces,n,n,2};
  assert(xk_feature_walk_state(&c,&g,g_xram+out));
  if(memcmp(&c,&original,sizeof c)||memcmp(g_xram,expected,0x300000)){
   fprintf(stderr,"walk mismatch trial %u\n",trial);
   for(unsigned i=0;i<sizeof c;i++)if(((uint8_t*)&c)[i]!=((uint8_t*)&original)[i])fprintf(stderr,"ctx byte %u got %x expected %x\n",i,((uint8_t*)&c)[i],((uint8_t*)&original)[i]);
   return 1;
  }
 }
 /* Reject malformed geometry and insufficient budget before any writes. */
 memcpy(expected,g_xram,0x300000);
 for(unsigned test=0;test<6;test++){
  memcpy(g_xram,expected,0x300000);put32(surfaces+4,0);
  xctx c={0};c.r[4]=sp;c.r[7]=geom;c.preempt=1000;
  xk_feature_geometry g={g_xram+vertices,g_xram+edges,g_xram+surfaces,8,8,2};
  if(test==0)put32(sp+4,2);
  if(test==1)put32(surfaces+4,8);
  if(test==2){put32(edges+16,2);put32(edges+20,3);}
  if(test==3){put32(edges,8);put32(edges+4,8);}
  if(test==4){put32(edges+8,1);put32(edges+12,1);put32(edges+24+8,1);put32(edges+24+12,1);}
  if(test==5)c.preempt=0;
  xctx unchanged=c;memcpy(before,g_xram,0x300000);
  assert(!xk_feature_walk_state(&c,&g,g_xram+out));
  assert(!memcmp(&c,&unchanged,sizeof c)&&!memcmp(g_xram,before,0x300000));
 }
 puts("PASS native walk: 6 unchanged-state malformed/budget refusals");
 puts("PASS native walk: 1000 full-context/full-arena cases, 1..8 vertices, mixed sides and arbitrary point bits");
 free(before);free(expected);free(g_xpt);free(g_xram);
}
