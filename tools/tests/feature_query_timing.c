/* Warm-cache query timing. Native mode assumes prior admission; this measures
 * its potential BEFORE implementing shared validation, not deployable cost. */
#define main feature_correctness_main
#include "feature_build_reference.c"
#undef main
#include "kernel/xk_feature_scope.h"
#ifdef XV_FEATURE_PRODUCTION_POINT
int xv_math_point_transform(xctx *);
void f_000B5EA0_reference(xctx *);
void f_000B5EA0(xctx *c){if(!xv_math_point_transform(c))f_000B5EA0_reference(c);}
void xk_os_log(const char *fmt,...){(void)fmt;}
#endif
static volatile uint32_t query_sink;
static void admitted_emit(xctx *c){if(!xk_feature_vertex_emit_state(c,g_xram+0x40000))abort();}
static void admitted_vertex(xctx *c){xk_feature_vertex_state(c,xk_feature_point_state,admitted_emit);}
static void native_query(xctx *c){xk_feature_query_state(c,admitted_vertex,f_000862A0,f_00086170);}
static void scoped_query(xctx *c){
 if(!xk_feature_try_query_vertices(c,0x300000,1<<20,f_000855F0,f_000862A0,f_00086170))f_000868F0(c);
}
static int compare_double(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
int main(void){
 g_xram=calloc(1,0x300000);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
 uint8_t *before=malloc(0x300000),*after=malloc(0x300000);assert(before&&after);
 for(unsigned i=0;i<0x300;i++)g_xpt[i]=4096*i;
 reference_init_constants();
#ifdef XV_FEATURE_PRODUCTION_POINT
 puts("baseline_point=production xk_math.c; object-lock contention excluded");
#endif
 const unsigned sp=0x10000,geom=0x20000,vertex=0x80000,edge=0x100000,surface=0x180000,q=0x30000,out=0x40000,matrix=0x60000;
 unsigned table_count=64;
 const char *requested=getenv("FEATURE_TABLE_SIZE");
 if(requested){char *end;unsigned long value=strtoul(requested,&end,10);assert(!*end&&value>=64&&value<=16384);table_count=(unsigned)value;}
 printf("geometry_table_entries=%u\n",table_count);
 put32(geom+0x54,table_count);put32(geom+0x48,table_count);put32(geom+0x3c,table_count);
 put32(geom+0x58,vertex);put32(geom+0x4c,edge);put32(geom+0x40,surface);
 for(unsigned i=0;i<64;i++){
  put32(vertex+i*16,bits((float)i/3));put32(vertex+i*16+4,bits((float)i/7));
  put32(vertex+i*16+8,bits((float)i/11));put32(vertex+i*16+12,0);
 }
 g_xram[surface+8]=3;g_xram[surface+9]=5;g_xram[surface+10]=7;
 put32(matrix,bits(2));put32(matrix+4,bits(1));put32(matrix+20,bits(1));put32(matrix+36,bits(1));
 put32(matrix+40,bits(10));
 for(unsigned i=0;i<256;i++)put32(q+0x80c+i*4,i%64);
 const unsigned sizes[4]={1,8,32,128};
 void (*paths[3])(xctx *)={f_000868F0,native_query,scoped_query};
 for(unsigned transform=0;transform<2;transform++)for(unsigned extrusion=0;extrusion<2;extrusion++)for(unsigned n=0;n<4;n++){
  put32(q+0x808,sizes[n]);put32(sp,0x12345678);put32(sp+4,geom);put32(sp+8,transform?matrix:0);
  put32(sp+12,bits(extrusion?2.f:0.f));put32(sp+16,bits(.5f));put32(sp+20,out);put32(out,0);
  xctx initial={0};initial.r[0]=0xffffffff;initial.r[4]=sp;initial.r[7]=q;initial.fcw=0x37f;initial.preempt=10000;
  /* Confirm whole context and arena equivalence for each timed fixture. */
  memcpy(before,g_xram,0x300000);xctx expected=initial;f_000868F0(&expected);memcpy(after,g_xram,0x300000);
  memcpy(g_xram,before,0x300000);xctx candidate=initial;native_query(&candidate);
  assert(memcmp(&candidate,&expected,sizeof expected)==0&&memcmp(g_xram,after,0x300000)==0);
  memcpy(g_xram,before,0x300000);candidate=initial;scoped_query(&candidate);
  assert(memcmp(&candidate,&expected,sizeof expected)==0&&memcmp(g_xram,after,0x300000)==0);
  memcpy(g_xram,before,0x300000);
  if(sizes[n]>1&&(transform||extrusion)){
   xk_feature_vertex_scope scope={0},untouched={0};
   assert(xk_feature_prepare_vertex_scope(&initial,0x300000,1<<20,f_000855F0,&scope));
   xctx rejected=initial;rejected.preempt=(int32_t)sizes[n]-1;xctx saved=rejected;
   scope=untouched;
   assert(!xk_feature_prepare_vertex_scope(&rejected,0x300000,1<<20,f_000855F0,&scope));
   assert(memcmp(&scope,&untouched,sizeof scope)==0&&memcmp(&rejected,&saved,sizeof saved)==0);
   assert(memcmp(g_xram,before,0x300000)==0);
   rejected=initial;rejected.preempt=(int32_t)sizes[n];
   assert(xk_feature_try_query_vertices(&rejected,0x300000,1<<20,f_000855F0,f_000862A0,f_00086170));
   assert(rejected.preempt==1); /* preserve all n-1 decrements without yielding */
   assert(memcmp(g_xram,after,0x300000)==0);
  }
  double samples[3][7];unsigned iterations=2000;
  for(unsigned round=0;round<8;round++)for(unsigned k=0;k<3;k++){
   unsigned mode=(k+round)%3;uint64_t start=ns_now();
   for(unsigned i=0;i<iterations;i++){
    put32(out,0);put32(sp+20,out);xctx c=initial;paths[mode](&c);query_sink=c.r[0]^get32(out);
   }
   if(round)samples[mode][round-1]=(double)(ns_now()-start)/iterations;
  }
  for(unsigned mode=0;mode<3;mode++)qsort(samples[mode],7,sizeof(double),compare_double);
  printf("vertices=%u transform=%u extrusion=%u original_ns=%.1f admitted_query_ns=%.1f ratio=%.3f scoped_ns=%.1f\n",sizes[n],transform,extrusion,samples[0][3],samples[1][3],samples[1][3]/samples[0][3],samples[2][3]);
 }
 free(before);free(after);free(g_xpt);free(g_xram);return 0;
}
