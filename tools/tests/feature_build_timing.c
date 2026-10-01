/* Supporting ARM microbenchmark, never a Vita frame-rate predictor.
 * Uses the same owned-image oracle linkage as feature_build_reference.c.
 * All paths include context/count reset; no clock calls inside the batch.
 * Run correctness first. The admitted-only path deliberately excludes guards. */
#define main feature_correctness_main
#include "feature_build_reference.c"
#undef main
static volatile uint32_t timing_sink;
static __attribute__((noinline)) void emit_original(xctx *c){f_000855F0(c);}
static __attribute__((noinline)) void emit_validated(xctx *c){
 if(!xk_feature_try_vertex_emit(c,0x300000,1<<20))abort();
}
static __attribute__((noinline)) void emit_admitted(xctx *c){
 if(!xk_feature_vertex_emit_state(c,g_xram+0x40000))abort();
}
static int order_double(const void *a,const void *b){
 double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);
}
int main(void){
 g_xram=calloc(1,0x300000);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
 for(unsigned i=0;i<0x300;i++)g_xpt[i]=4096*i;
 reference_init_constants();
 const unsigned sp=0x10000,out=0x40000,point=0x21000,iterations=50000;
 put32(point,bits(1.25f));put32(point+4,bits(-2.75f));put32(point+8,bits(3.125f));
 put32(sp,0x12345678);put32(sp+8,bits(.5f));put32(sp+12,0xabcdef);
 put32(sp+16,0xffffffff);put32(sp+20,3);put32(sp+24,5);
 void (*paths[3])(xctx *)={emit_original,emit_validated,emit_admitted};
 const char *names[3]={"original","validated_native","admitted_native"};
 const unsigned capacities[3]={0,255,256};
 for(unsigned extrude=0;extrude<2;extrude++)for(unsigned ci=0;ci<3;ci++){
  put32(sp+4,bits(extrude?2.f:0.f));
  xctx initial={0};initial.r[0]=0xdeadbeef;initial.r[1]=out;initial.r[2]=19;
  initial.r[3]=11;initial.r[4]=sp;initial.r[5]=37;initial.r[6]=point;initial.r[7]=7;
  initial.fcw=0x37f;initial.fsw=0x713;initial.fsp=3;
  for(unsigned i=0;i<8;i++)initial.st[i]=i+.375;
  double samples[3][7];
  for(unsigned round=0;round<8;round++)for(unsigned k=0;k<3;k++){
   unsigned mode=(k+round)%3;
   uint64_t start=ns_now();
   for(unsigned i=0;i<iterations;i++){
    xctx c=initial;unsigned capacity=capacities[ci];
    put32(out,capacity|(capacity<<16));
    paths[mode](&c);
    timing_sink=c.r[0]^c.r[1]^c.r[2]^get32(out);
   }
   double ns=(double)(ns_now()-start)/iterations;
   if(round)samples[mode][round-1]=ns; /* first round warms every path */
  }
  for(unsigned mode=0;mode<3;mode++){
   qsort(samples[mode],7,sizeof(double),order_double);
   printf("{\"path\":\"%s\",\"extrude\":%u,\"capacity\":%u,\"median_ns\":%.1f,\"min_ns\":%.1f,\"max_ns\":%.1f,\"iterations_per_sample\":%u}\n",
    names[mode],extrude,capacities[ci],samples[mode][3],samples[mode][0],samples[mode][6],iterations);
  }
 }
 free(g_xpt);free(g_xram);return 0;
}
