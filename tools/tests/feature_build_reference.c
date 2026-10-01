/* Owned-image oracle cases and native output-component differential checks. */
#include "xv_x86rt.h"
#include "kernel/xk_feature_build.h"
#include "kernel/xk_feature_state.h"
#include "kernel/xk_feature_admission.h"
#include "kernel/xk_feature_scope.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
#ifdef XV_CHECK_GUEST_ADDRESS
uint32_t xv_trash_off=0x2ff000,xv_page_epoch[XV_PAGE_EPOCH_ENTRIES];
uint32_t xv_page_count=0x300,xv_write_epoch=7,xv_watch_off,xv_watch_len;
void xv_check_guest_address(uint32_t a){(void)a;abort();}
void xv_watch_store(uint32_t a,uint32_t o){(void)a;(void)o;abort();}
void xv_mark_written(const void *p,uint32_t n){
 size_t off=(const uint8_t *)p-g_xram;assert(n&&off+n<=0x300000);
 for(size_t page=off>>12;page<=(off+n-1)>>12;page++)xv_page_epoch[page]=xv_write_epoch;
}
#endif
void x_guest_read_pages(void *out,uint32_t a,size_t n){assert(a+n<=0x300000);memcpy(out,g_xram+a,n);}
void x_guest_write_pages(uint32_t a,const void *in,size_t n){assert(a+n<=0x300000);memcpy(g_xram+a,in,n);}
static unsigned preempt_test_enabled,preempt_test_calls;
void xv_preempt(xctx *c){
 if(!preempt_test_enabled)abort();
 preempt_test_calls++;c->preempt=17;
}
void f_000868F0(xctx *);
void f_000855F0(xctx *);
void f_00085020(xctx *);
void f_000851E0(xctx *);
void f_00086A40(xctx *);
void f_000B5EA0(xctx *);
void f_000B5E40(xctx *);
void f_00086170(xctx *);
void f_000862A0(xctx *);
void f_00086440(xctx *);
#include "reference_constants.h"
static void vertex_emit_candidate(xctx *c){
#ifdef XV_CHECK_GUEST_ADDRESS
 uint32_t output=c->r[1];
 for(uint32_t page=output>>12;page<=(output+0x4407)>>12;page++)xv_page_epoch[g_xpt[page]>>12]=0;
#endif
 assert(xk_feature_try_vertex_emit(c,0x300000,1<<20));
#ifdef XV_CHECK_GUEST_ADDRESS
 for(uint32_t page=output>>12;page<=(output+0x4407)>>12;page++)
  assert(xv_page_epoch[g_xpt[page]>>12]==xv_write_epoch);
#endif
}
static unsigned vertex_boundary_cases;
static void vertex_state_candidate(xctx *c){
 /* Compare at the child return, before outer code can overwrite mismatches. */
 if(vertex_boundary_cases<256){
  static uint8_t input[0x300000],expected[0x300000];
  xctx original=*c;
  memcpy(input,g_xram,sizeof input);f_00086440(&original);
  memcpy(expected,g_xram,sizeof expected);memcpy(g_xram,input,sizeof input);
  xk_feature_vertex_state(c,xk_feature_point_state,vertex_emit_candidate);
  assert(memcmp(c,&original,sizeof original)==0);
  assert(memcmp(g_xram,expected,sizeof expected)==0);
  vertex_boundary_cases++;
 }else xk_feature_vertex_state(c,xk_feature_point_state,vertex_emit_candidate);
}
/* Independent scopes prove callback routing without global query state. */
typedef struct { unsigned phase,calls[3]; } query_route_test;
static void query_route_vertex(xctx *c,void *opaque){
 query_route_test *s=opaque;assert(s->phase==0);s->calls[0]++;f_00086440(c);
}
static void query_route_edge(xctx *c,void *opaque){
 query_route_test *s=opaque;assert(s->phase<=1);s->phase=1;s->calls[1]++;f_000862A0(c);
}
static void query_route_surface(xctx *c,void *opaque){
 query_route_test *s=opaque;s->phase=2;s->calls[2]++;f_00086170(c);
}
static void put32(unsigned a,uint32_t v){memcpy(g_xram+a,&v,4);}
static uint32_t get32(unsigned a){uint32_t v;memcpy(&v,g_xram+a,4);return v;}
static uint32_t bits(float f){uint32_t v;memcpy(&v,&f,4);return v;}
static uint64_t ns_now(void){struct timespec t;assert(clock_gettime(CLOCK_MONOTONIC,&t)==0);return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
int main(void){
 g_xram=calloc(1,0x300000);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
 reference_init_constants();
 for(unsigned i=0;i<0x300;i++)g_xpt[i]=i*4096;
 const unsigned sp=0x10000,geom=0x20000,vertex=0x21000,edge=0x22000,surface=0x23000,q=0x30000,out=0x40000,matrix=0x60000;
 put32(geom+0x58,vertex);put32(geom+0x4c,edge);put32(geom+0x40,surface);
 put32(vertex,0x3f800000);put32(vertex+4,0x40000000);put32(vertex+8,0x40400000);
 g_xram[surface+8]=3;g_xram[surface+9]=5;g_xram[surface+10]=7;
 const unsigned capacities[]={0,255,256};
 unsigned cases=0;
 for(unsigned transform=0;transform<4;transform++)
 for(unsigned object=0;object<2;object++)
 for(unsigned count=0;count<=1;count++)
 for(unsigned extrude=0;extrude<=1;extrude++)
 for(unsigned si=0;si<3;si++)
 for(unsigned ci=0;ci<3;ci++){
  unsigned ns=capacities[si],nc=capacities[ci];
  uint32_t handle=object?0x12345678:0xffffffff, surface_id=object?0xffffffff:0;
  /* Identity; translated identity; scaled 90-degree rotation about Z.
     Expected points are hand-computed, independent of the guest math helper. */
  float px=1,py=2,pz=3;
  memset(g_xram+matrix,0,52);
  put32(matrix,bits(1));put32(matrix+4,bits(1));
  put32(matrix+20,bits(1));put32(matrix+36,bits(1));
  if(transform==2){
   put32(matrix+40,bits(10));put32(matrix+44,bits(-4));put32(matrix+48,bits(2));
   px=11;py=-2;pz=5;
  }else if(transform==3){
   put32(matrix,bits(2));put32(matrix+4,0);put32(matrix+20,0);
   put32(matrix+8,bits(1));put32(matrix+16,bits(-1));
   px=-4;py=2;pz=6;
  }
  memset(g_xram+q,0,0x1010);memset(g_xram+out,0,0x5000);
  put32(out,ns | (nc<<16));
  put32(q+0x808,count);put32(q+0x80c,0);
  put32(sp,0x12345678);put32(sp+4,geom);put32(sp+8,transform?matrix:0);
  put32(sp+12,extrude?0x40000000:0);put32(sp+16,0x3f000000);put32(sp+20,out);
  xctx c={0};c.r[0]=handle;c.r[3]=0xabc;c.r[4]=sp;c.r[5]=0xdef;c.r[6]=0x123;c.r[7]=q;c.preempt=10000;
  uint8_t native_output[0x5000];
  memcpy(native_output,g_xram+out,sizeof native_output);
  if(count){
   xk_feature_geometry geometry={g_xram+vertex,g_xram+edge,g_xram+surface,1,1,1};
   float matrix_values[13];memcpy(matrix_values,g_xram+matrix,sizeof matrix_values);
   assert(xk_feature_vertex_from_geometry(native_output,&geometry,0,handle,
          transform?matrix_values:NULL,extrude?2:0,.5f));
  }
  f_000868F0(&c);
  assert(memcmp(native_output,g_xram+out,sizeof native_output)==0);
  assert(c.r[4]==sp+24 && c.r[3]==0xabc && c.r[5]==0xdef && c.r[6]==0x123 && c.r[7]==q);
  unsigned expected_s=ns+count*(1+extrude),expected_c=nc+count*extrude;
  if(expected_s>256)expected_s=256;
  if(expected_c>256)expected_c=256;
  assert((get32(out)&0xffff)==expected_s);
  assert((get32(out)>>16)==expected_c);
  for(unsigned i=ns;i<expected_s;i++){
   unsigned r=out+8+i*28;
   assert(get32(r)==handle && get32(r+4)==surface_id);
   assert(g_xram[r+8]==3 && g_xram[r+9]==5 && g_xram[r+10]==7);
   assert(get32(r+12)==bits(px) && get32(r+16)==bits(py));
   assert(get32(r+20)==bits(i==ns?pz:pz-2));
   assert(get32(r+24)==0x3f000000);
  }
  if(expected_c>nc){
   unsigned r=out+0x1c08+nc*40;
   assert(get32(r)==handle && get32(r+4)==surface_id);
   assert(g_xram[r+8]==3 && g_xram[r+9]==5 && g_xram[r+10]==7);
   assert(get32(r+12)==bits(px) && get32(r+16)==bits(py) && get32(r+20)==bits(pz-2));
   assert(get32(r+24)==0 && get32(r+28)==0);
   assert(get32(r+32)==0x40000000 && get32(r+36)==0x3f000000);
  }
  cases++;

 }
 /* A coplanar internal edge contributes no capsule. Opposite signed
    references to the same plane contribute one boundary capsule. */
 unsigned edge_cases=0;
 put32(geom+0x10,0x24000);
 put32(0x24000,bits(0));put32(0x24004,bits(0));put32(0x24008,bits(1));
 put32(vertex+16,bits(4));put32(vertex+20,bits(6));put32(vertex+24,bits(3));
 put32(edge,0);put32(edge+4,1);put32(edge+16,0);put32(edge+20,1);
 for(unsigned boundary=0;boundary<2;boundary++)
 for(unsigned object=0;object<2;object++)
 for(unsigned ci=0;ci<3;ci++){
  unsigned nc=capacities[ci];
  uint32_t handle=object?0x12345678:0xffffffff;
  put32(surface,0);put32(surface+12,boundary?0x80000000:0);
  memset(g_xram+q,0,0x1010);memset(g_xram+out,0,0x5000);
  put32(out,nc<<16);put32(q+0x404,1);put32(q+0x408,0);
  put32(sp,0x12345678);put32(sp+4,geom);put32(sp+8,0);
  put32(sp+12,0);put32(sp+16,bits(.5f));put32(sp+20,out);
  xctx c={0};c.r[0]=handle;c.r[3]=0xabc;c.r[4]=sp;c.r[5]=0xdef;c.r[6]=0x123;c.r[7]=q;c.preempt=10000;
  f_000868F0(&c);
  assert(c.r[4]==sp+24 && c.r[3]==0xabc && c.r[5]==0xdef && c.r[6]==0x123 && c.r[7]==q);
  unsigned appended=boundary && nc<256;
  assert(get32(out)==((nc+appended)<<16));
  if(appended){
   unsigned r=out+0x1c08+nc*40;
   assert(get32(r)==handle && get32(r+4)==(object?0xffffffff:0));
   assert(g_xram[r+8]==3 && g_xram[r+9]==5 && g_xram[r+10]==7);
   assert(get32(r+12)==bits(1) && get32(r+16)==bits(2) && get32(r+20)==bits(3));
   assert(get32(r+24)==bits(3) && get32(r+28)==bits(4) && get32(r+32)==0);
   assert(get32(r+36)==bits(.5f));
  }
  edge_cases++;
 }
 printf("PASS feature reference: %u coplanar/boundary edge cases\n",edge_cases);
 unsigned surface_cases=0;
 put32(vertex+32,bits(1));put32(vertex+36,bits(6));put32(vertex+40,bits(3));
 put32(0x2400c,bits(3));put32(surface,0);put32(surface+4,0);
 for(unsigned i=0;i<3;i++){
  unsigned e=edge+i*24;
  put32(e,i);put32(e+4,(i+1)%3);put32(e+8,(i+1)%3);
  put32(e+12,(i+2)%3);put32(e+16,0);put32(e+20,1);
 }
 for(unsigned object=0;object<2;object++)
 for(unsigned pi=0;pi<3;pi++){
  unsigned np=capacities[pi];
  uint32_t handle=object?0x12345678:0xffffffff;
  memset(g_xram+q,0,0x1010);memset(g_xram+out,0,0xb000);
  put32(out+4,np);put32(q,1);put32(q+4,0);
  put32(sp,0x12345678);put32(sp+4,geom);put32(sp+8,0);
  put32(sp+12,0);put32(sp+16,bits(.5f));put32(sp+20,out);
  xctx c={0};c.r[0]=handle;c.r[3]=0xabc;c.r[4]=sp;c.r[5]=0xdef;c.r[6]=0x123;c.r[7]=q;c.preempt=10000;
  uint8_t native_surface_output[0xb000];memcpy(native_surface_output,g_xram+out,sizeof native_surface_output);
  xk_feature_geometry geometry={g_xram+vertex,g_xram+edge,g_xram+surface,3,3,2};
  float points[8][3],plane[4];unsigned n=0;
  assert(xk_feature_surface_points(&geometry,0,points,&n));
  memcpy(plane,g_xram+0x24000,sizeof plane);
  xk_feature_metadata meta={handle,object?0xffffffff:0,3,5,7};
  assert(xk_feature_build_surface(native_surface_output,&meta,points,n,plane,0,.5f));
  f_000868F0(&c);
  assert(memcmp(native_surface_output,g_xram+out,sizeof native_surface_output)==0);
  assert(c.r[4]==sp+24 && c.r[3]==0xabc && c.r[5]==0xdef && c.r[6]==0x123 && c.r[7]==q);
  assert(get32(out)==0 && get32(out+4)==np+(np<256));
  if(np<256){
   unsigned r=out+0x4408+np*104;
   assert(get32(r)==handle && get32(r+4)==(object?0xffffffff:0));
   assert(g_xram[r+8]==3 && g_xram[r+9]==5 && g_xram[r+10]==7);
   assert(get32(r+12)==0 && get32(r+16)==0 && get32(r+20)==bits(1) && get32(r+24)==bits(3));
   assert(get32(r+28)==bits(.5f));
   assert((get32(r+32)&0xffffff)==0x10002 && get32(r+36)==3);
   const float xy[6]={1,2,4,6,1,6};
   for(unsigned i=0;i<6;i++)assert(get32(r+40+i*4)==bits(xy[i]));
  }
  surface_cases++;
 }
 printf("PASS feature reference: %u triangle surface cases\n",surface_cases);
 /* Deterministic output differential for the native vertex append component.
    Existing records and unused output bytes are deliberately nonzero. */
 uint32_t rng=0x1234abcd;
 for(unsigned trial=0;trial<10000;trial++){
  float point[3];
  for(unsigned i=0;i<3;i++){
   rng=rng*1664525u+1013904223u;
   point[i]=(float)((int32_t)(rng>>8)-0x800000)/4096.f;
  }
  float height=(float)((int)(trial%31)-10)/8.f, radius=(float)(trial%29)/16.f;
  xk_feature_metadata meta={rng,rng^0xabcdef01,(uint8_t)rng,(uint8_t)(rng>>8),(uint16_t)(rng>>16)};
  uint8_t native_output[0x5000];
  memset(g_xram+out,(uint8_t)trial,sizeof native_output);
  put32(out,(trial%257)|((trial*13%257)<<16));
  memcpy(native_output,g_xram+out,sizeof native_output);
  memcpy(g_xram+vertex,point,12);
  put32(sp,0x12345678);put32(sp+4,bits(height));put32(sp+8,bits(radius));
  put32(sp+12,meta.object);put32(sp+16,meta.surface);
  put32(sp+20,meta.flags_a);put32(sp+24,meta.flags_b);
  xctx c={0};c.r[1]=out;c.r[4]=sp;c.r[6]=vertex;c.r[7]=meta.material;c.preempt=10000;
  c.r[0]=rng;c.r[2]=rng^0x1234;c.r[3]=rng+1;c.r[5]=rng+2;
  c.fsp=trial%8;c.fcw=0x37f;c.fsw=(uint16_t)rng;
  for(unsigned i=0;i<8;i++)c.st[i]=i+.875;
  xctx initial=c;
  uint8_t initial_output[0x5000],expected_stack[8];
  memcpy(initial_output,native_output,sizeof initial_output);
  assert(xk_feature_build_vertex(native_output,&meta,point,height,radius));
  f_000855F0(&c);
  memcpy(expected_stack,g_xram+sp-8,8);
  assert(c.r[4]==sp+28);
  assert(memcmp(native_output,g_xram+out,sizeof native_output)==0);
  memcpy(g_xram+out,initial_output,sizeof initial_output);
  memset(g_xram+sp-8,0xa5,8);
  vertex_emit_candidate(&initial);
  assert(memcmp(&initial,&c,sizeof c)==0);
  assert(memcmp(native_output,g_xram+out,sizeof native_output)==0);
  assert(memcmp(expected_stack,g_xram+sp-8,8)==0);
 }
 puts("PASS native vertex output: 10000 deterministic differential cases");
 for(unsigned trial=0;trial<10000;trial++){
  float points[8][3],plane[4];
  for(unsigned i=0;i<8;i++)for(unsigned j=0;j<3;j++){
   rng=rng*1664525u+1013904223u;
   points[i][j]=(float)((int32_t)(rng>>8)-0x800000)/4096.f;
  }
  for(unsigned i=0;i<4;i++)plane[i]=(float)((int)((trial>>(i*3))%7)-3)/4.f;
  unsigned count=trial%9,initial=trial%257;
  float height=(float)((int)(trial%31)-10)/8.f,radius=(float)(trial%29)/16.f;
  xk_feature_metadata meta={rng,rng^0xabcdef01,(uint8_t)rng,(uint8_t)(rng>>8),(uint16_t)(rng>>16)};
  uint8_t native_output[0xb000];
  memset(g_xram+out,(uint8_t)trial,sizeof native_output);
  put32(out+4,initial);memcpy(native_output,g_xram+out,sizeof native_output);
  memcpy(g_xram+vertex,points,sizeof points);memcpy(g_xram+0x24000,plane,sizeof plane);
  put32(sp,0x12345678);put32(sp+4,count);put32(sp+8,vertex);put32(sp+12,0x24000);
  put32(sp+16,bits(height));put32(sp+20,bits(radius));put32(sp+24,meta.object);
  put32(sp+28,meta.surface);put32(sp+32,meta.flags_a);put32(sp+36,meta.flags_b);
  put32(sp+40,meta.material);put32(sp+44,out);
  xctx c={0};c.r[4]=sp;c.preempt=10000;
  assert(xk_feature_build_surface(native_output,&meta,points,count,plane,height,radius));
  f_00085020(&c);
  assert(c.r[4]==sp+48);
  if(memcmp(native_output,g_xram+out,sizeof native_output)){
   fprintf(stderr,"surface mismatch trial=%u count=%u plane=%g,%g,%g height=%g\n",trial,count,plane[0],plane[1],plane[2],height);
   abort();
  }
 }
 puts("PASS native surface output: 10000 deterministic differential cases");
 double epsilon;memcpy(&epsilon,g_xram+0x1f0af8,8);
 for(unsigned trial=0;trial<10000;trial++){
  float point[3],direction[3];
  for(unsigned i=0;i<3;i++){
   rng=rng*1664525u+1013904223u;
   point[i]=(float)((int32_t)(rng>>8)-0x800000)/4096.f;
   direction[i]=(float)((int)((trial>>(i*3))%7)-3)/4.f;
  }
  float height=(float)((int)(trial%31)-10)/8.f,radius=(float)(trial%29)/16.f;
  xk_feature_metadata meta={rng,rng^0xabcdef01,(uint8_t)rng,(uint8_t)(rng>>8),(uint16_t)(rng>>16)};
  uint8_t native_output[0xb000];
  memset(g_xram+out,(uint8_t)trial,sizeof native_output);
  put32(out,(trial%257)<<16);put32(out+4,trial*13%257);
  memcpy(native_output,g_xram+out,sizeof native_output);
  memcpy(g_xram+vertex,point,12);memcpy(g_xram+0x24000,direction,12);
  put32(sp,0x12345678);put32(sp+4,bits(height));put32(sp+8,bits(radius));
  put32(sp+12,meta.object);put32(sp+16,meta.surface);put32(sp+20,meta.flags_a);
  put32(sp+24,meta.flags_b);put32(sp+28,meta.material);
  xctx c={0};c.r[0]=out;c.r[1]=vertex;c.r[2]=0x24000;c.r[4]=sp;c.preempt=10000;
  assert(xk_feature_build_edge(native_output,&meta,point,direction,height,radius,epsilon));
  f_000851E0(&c);
  assert(c.r[4]==sp+32);
  if(memcmp(native_output,g_xram+out,sizeof native_output)){
   fprintf(stderr,"edge mismatch trial=%u direction=%g,%g,%g height=%g\n",trial,direction[0],direction[1],direction[2],height);
   for(unsigned i=0;i<sizeof native_output;i++)if(native_output[i]!=g_xram[out+i]){fprintf(stderr,"offset=%x native=%x guest=%x\n",i,native_output[i],g_xram[out+i]);break;}
   abort();
  }
 }
 puts("PASS native edge output: 10000 deterministic differential cases");
 unsigned walk_cases=0;
 for(unsigned n=3;n<=8;n++)for(unsigned side=0;side<2;side++){
  put32(surface+4,0);
  for(unsigned i=0;i<n;i++){
   for(unsigned j=0;j<3;j++)put32(vertex+16*i+4*j,bits((float)(i*3+j)-7.5f));
   unsigned e=edge+24*i;
   put32(e,side?(i+1)%n:i);put32(e+4,side?i:(i+1)%n);
   put32(e+8,(i+1)%n);put32(e+12,(i+1)%n);
   put32(e+16,side?1:0);put32(e+20,side?0:1);
  }
  xk_feature_geometry geometry={g_xram+vertex,g_xram+edge,g_xram+surface,n,n,2};
  float actual[8][3];memset(actual,0xa5,sizeof actual);unsigned count=0;
  assert(xk_feature_surface_points(&geometry,0,actual,&count));assert(count==n);
  memset(g_xram+0x25000,0xa5,sizeof actual);
  put32(sp,0x12345678);put32(sp+4,0);put32(sp+8,0x25000);
  xctx c={0};c.r[7]=geom;c.r[4]=sp;c.preempt=10000;
  f_00086A40(&c);
  assert((c.r[0]&0xffff)==n && c.r[4]==sp+12);
  assert(memcmp(actual,g_xram+0x25000,sizeof actual)==0);
  /* A bad endpoint must not publish a partial polygon. */
  float saved[8][3];memcpy(saved,actual,sizeof saved);unsigned saved_count=count;
  put32(edge+4*side,n);
  assert(!xk_feature_surface_points(&geometry,0,actual,&count));
  assert(count==saved_count && memcmp(actual,saved,sizeof saved)==0);
  put32(edge+4*side,0);
  put32(edge+24+8+4*side,1); /* cycle that never returns to initial edge */
  assert(!xk_feature_surface_points(&geometry,0,actual,&count));
  assert(count==saved_count && memcmp(actual,saved,sizeof saved)==0);
  put32(edge+24+8+4*side,2);
  put32(edge+16,1);put32(edge+20,1); /* edge belongs to neither side */
  assert(!xk_feature_surface_points(&geometry,0,actual,&count));
  assert(count==saved_count && memcmp(actual,saved,sizeof saved)==0);
  walk_cases++;
 }
 printf("PASS native surface traversal: %u left/right polygon and invalid-index cases\n",walk_cases);
 for(unsigned trial=0;trial<10000;trial++)for(unsigned translate=0;translate<2;translate++){
  float point[3],m[13],actual[3];
  for(unsigned i=0;i<16;i++){
   rng=rng*1664525u+1013904223u;
   float f=(float)((int32_t)(rng>>8)-0x800000)/65536.f;
   if(i<3)point[i]=f;else m[i-3]=f;
  }
  if(trial%3==0)m[0]=1;
  memcpy(g_xram+vertex,point,12);memcpy(g_xram+matrix,m,sizeof m);
  put32(sp,0x12345678);
  xctx c={0};c.r[0]=0x25000;c.r[1]=matrix;c.r[2]=vertex;c.r[4]=sp;c.preempt=10000;
  c.fsp=trial%8;c.fcw=0x37f;c.fsw=0x4321;
  for(unsigned i=0;i<8;i++)c.st[i]=i+.125;
  xctx initial=c;
  xk_feature_transform(actual,point,m,translate);
  if(translate)f_000B5EA0(&c);else f_000B5E40(&c);
  assert(c.r[4]==sp+4 && memcmp(actual,g_xram+0x25000,12)==0);
  if(translate){
   xctx candidate=initial;xk_feature_point_state(&candidate);
   assert(memcmp(&candidate,&c,sizeof c)==0);
   assert(memcmp(actual,g_xram+0x25000,12)==0);
  }
  float inplace[3];memcpy(inplace,point,12);xk_feature_transform(inplace,inplace,m,translate);
  assert(memcmp(inplace,actual,12)==0);
 }
 puts("PASS native transforms: 20000 point/vector differential cases and in-place checks");
 /* Output may alias input or matrix: preserve read/store ordering, not just math. */
 {
  uint8_t *before=malloc(0x300000),*after=malloc(0x300000);assert(before&&after);
  for(unsigned trial=0;trial<256;trial++){
   for(unsigned i=0;i<13;i++)put32(matrix+i*4,bits((float)((int)i-5)/8.f));
   if(trial%2==0)put32(matrix,bits(1));
   for(unsigned i=0;i<3;i++)put32(vertex+i*4,bits((float)(i+1)/3.f));
   xctx original={0};original.r[0]=trial%2?matrix+4*(trial%13):vertex+4*(trial%3);
   original.r[1]=matrix;original.r[2]=vertex;original.r[4]=sp;
   original.fsp=trial%8;original.fcw=0x37f;original.fsw=0x713;
   for(unsigned i=0;i<8;i++)original.st[i]=i+.375;
   xctx candidate=original;memcpy(before,g_xram,0x300000);
   f_000B5EA0(&original);memcpy(after,g_xram,0x300000);
   memcpy(g_xram,before,0x300000);xk_feature_point_state(&candidate);
   assert(memcmp(&candidate,&original,sizeof original)==0);
   assert(memcmp(g_xram,after,0x300000)==0);
  }
  free(before);free(after);
 }
 puts("PASS point state: 10000 full-context cases and 256 full-arena alias cases");

 /* Full single-surface geometry path, including signed planes and transforms. */
 put32(geom+0x10,0x24000);put32(surface+4,0);
 for(unsigned i=0;i<3;i++){
  unsigned e=edge+i*24;
  put32(e,i);put32(e+4,(i+1)%3);put32(e+8,(i+1)%3);
  put32(e+12,(i+2)%3);put32(e+16,0);put32(e+20,1);
 }
 for(unsigned trial=0;trial<2000;trial++){
  float m[13],plane[4];
  for(unsigned i=0;i<13;i++){
   rng=rng*1664525u+1013904223u;m[i]=(float)((int)(rng%10001)-5000)/1024.f;
  }
  for(unsigned i=0;i<4;i++){
   rng=rng*1664525u+1013904223u;plane[i]=(float)((int)(rng%10001)-5000)/1024.f;
  }
  for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)put32(vertex+16*i+4*j,bits((float)(i*3+j)-7.5f));
  put32(surface,trial&1?0x80000000:0);
  memcpy(g_xram+matrix,m,sizeof m);memcpy(g_xram+0x24000,plane,sizeof plane);
  float height=(float)((int)(trial%31)-10)/8.f,radius=.5f;
  uint32_t object=trial%3?0xffffffff:0xabcdef01;
  uint8_t actual[0xb000];memset(g_xram+out,(uint8_t)trial,sizeof actual);
  put32(out+4,trial%257);memcpy(actual,g_xram+out,sizeof actual);
  xk_feature_geometry geometry={g_xram+vertex,g_xram+edge,g_xram+surface,3,3,2,g_xram+0x24000,1};
  assert(xk_feature_surface_from_geometry(actual,&geometry,0,object,trial%5?m:NULL,height,radius));
  put32(sp,0x12345678);put32(sp+4,0);put32(sp+8,bits(height));put32(sp+12,bits(radius));
  put32(sp+16,object);put32(sp+20,out);
  xctx c={0};c.r[0]=geom;c.r[6]=trial%5?matrix:0;c.r[4]=sp;c.preempt=10000;
  f_00086170(&c);assert(c.r[4]==sp+24);
  if(memcmp(actual,g_xram+out,sizeof actual)){
   fprintf(stderr,"surface geometry mismatch trial=%u\n",trial);
   for(unsigned i=0;i<sizeof actual;i++)if(actual[i]!=g_xram[out+i]){fprintf(stderr,"offset=%x native=%x guest=%x\n",i,actual[i],g_xram[out+i]);break;}
   abort();
  }
 }
 puts("PASS native surface geometry: 2000 signed-plane/transform differential cases");
 float negative_threshold,positive_threshold;
 memcpy(&negative_threshold,g_xram+0x1f0c24,4);memcpy(&positive_threshold,g_xram+0x1f0b08,4);
 unsigned emitted=0,omitted=0;
 for(unsigned trial=0;trial<4000;trial++){
  float m[13],planes[8];
  for(unsigned i=0;i<13;i++){rng=rng*1664525u+1013904223u;m[i]=(float)((int)(rng%10001)-5000)/1024.f;}
  for(unsigned i=0;i<8;i++){rng=rng*1664525u+1013904223u;planes[i]=(float)((int)(rng%10001)-5000)/1024.f;}
  for(unsigned i=0;i<2;i++)for(unsigned j=0;j<3;j++){
   rng=rng*1664525u+1013904223u;put32(vertex+16*i+4*j,bits((float)((int)(rng%10001)-5000)/1024.f));
  }
  put32(edge,0);put32(edge+4,1);put32(edge+16,0);put32(edge+20,1);
  put32(surface,trial&1?0x80000000:0);
  put32(surface+12,(trial&2?0x80000000:0)|(trial%3?1:0));
  memcpy(g_xram+matrix,m,sizeof m);memcpy(g_xram+0x24000,planes,sizeof planes);
  float height=(float)((int)(trial%31)-10)/8.f,radius=.5f;
  uint32_t object=trial%3?0xffffffff:0xabcdef01;
  uint8_t actual[0xb000];memset(g_xram+out,(uint8_t)trial,sizeof actual);
  put32(out,(trial%257)<<16);put32(out+4,trial*13%257);memcpy(actual,g_xram+out,sizeof actual);
  xk_feature_geometry geometry={g_xram+vertex,g_xram+edge,g_xram+surface,2,1,2,g_xram+0x24000,2};
  assert(xk_feature_edge_from_geometry(actual,&geometry,0,object,trial%5?m:NULL,height,radius,
                                     negative_threshold,positive_threshold,epsilon));
  if(memcmp(actual,g_xram+out,sizeof actual))emitted++;else omitted++;
  put32(sp,0x12345678);put32(sp+4,trial%5?matrix:0);put32(sp+8,bits(height));put32(sp+12,bits(radius));
  put32(sp+16,object);put32(sp+20,out);
  xctx c={0};c.r[0]=0;c.r[1]=geom;c.r[4]=sp;c.preempt=10000;
  f_000862A0(&c);assert(c.r[4]==sp+24);
  if(memcmp(actual,g_xram+out,sizeof actual)){
   fprintf(stderr,"edge geometry mismatch trial=%u\n",trial);
   for(unsigned i=0;i<sizeof actual;i++)if(actual[i]!=g_xram[out+i]){fprintf(stderr,"offset=%x native=%x guest=%x\n",i,actual[i],g_xram[out+i]);break;}
   abort();
  }
 }
 assert(emitted && omitted);
 printf("PASS native edge geometry: %u emitted and %u omitted differential cases\n",emitted,omitted);
 /* Mixed lists deliberately repeat entries: deduplication would change the
    capacity/order semantics and must not be introduced by the native path. */
 put32(surface+4,0);put32(surface+16,0);
 put32(geom+0x54,3);put32(geom+0x48,3);put32(geom+0x3c,2);
 for(unsigned i=0;i<3;i++){
  unsigned e=edge+24*i;
  put32(e,i);put32(e+4,(i+1)%3);put32(e+8,(i+1)%3);put32(e+12,(i+2)%3);
  put32(e+16,0);put32(e+20,1);put32(vertex+16*i+12,i);
 }
 uint64_t native_ns=0,guest_ns=0;
 uint8_t *before_arena=malloc(0x300000),*after_arena=malloc(0x300000);assert(before_arena&&after_arena);
 unsigned register_mask=0,x87_slot_mask=0,deepest_stack_change=0,max_budget_used=0;
 unsigned changed_fsw=0,changed_flags=0,scoped_queries=0,scoped_fallbacks=0;
 for(unsigned trial=0;trial<1000;trial++){
  float m[13],planes[8];
  for(unsigned i=0;i<13;i++){rng=rng*1664525u+1013904223u;m[i]=(float)((int)(rng%10001)-5000)/1024.f;}
  for(unsigned i=0;i<8;i++){rng=rng*1664525u+1013904223u;planes[i]=(float)((int)(rng%10001)-5000)/1024.f;}
  for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++){
   rng=rng*1664525u+1013904223u;put32(vertex+16*i+4*j,bits((float)((int)(rng%10001)-5000)/1024.f));
  }
  put32(surface,trial&1?0x80000000:0);put32(surface+12,(trial&2?0x80000000:0)|1);
  g_xram[surface+20]=11;g_xram[surface+21]=13;g_xram[surface+22]=17;
  memcpy(g_xram+matrix,m,sizeof m);memcpy(g_xram+0x24000,planes,sizeof planes);
  uint32_t vertices[12],edges[12],surfaces[12];
  for(unsigned i=0;i<12;i++){
   vertices[i]=(trial+i*2)%3;edges[i]=(trial+i)%3;surfaces[i]=(trial+i)%2;
  }
  xk_feature_query query={vertices,edges,surfaces,trial%13,(trial/3)%13,(trial/7)%13};
  memset(g_xram+q,0,0x1010);
  put32(q,query.surface_count);memcpy(g_xram+q+4,surfaces,query.surface_count*4);
  put32(q+0x404,query.edge_count);memcpy(g_xram+q+0x408,edges,query.edge_count*4);
  put32(q+0x808,query.vertex_count);memcpy(g_xram+q+0x80c,vertices,query.vertex_count*4);
  float height=(float)((int)(trial%31)-10)/8.f,radius=.5f;
  uint32_t object=trial%3?0xffffffff:0xabcdef01;
  uint8_t actual[0xb000];memset(g_xram+out,(uint8_t)trial,sizeof actual);
  put32(out,(trial%257)|((trial*7%257)<<16));put32(out+4,trial*13%257);memcpy(actual,g_xram+out,sizeof actual);
  xk_feature_geometry geometry={g_xram+vertex,g_xram+edge,g_xram+surface,3,3,2,g_xram+0x24000,2};
  uint64_t started=ns_now();
  assert(xk_feature_build_query(actual,&geometry,&query,object,trial%5?m:NULL,height,radius,
                                negative_threshold,positive_threshold,epsilon));
  native_ns+=ns_now()-started;
  put32(sp,0x12345678);put32(sp+4,geom);put32(sp+8,trial%5?matrix:0);put32(sp+12,bits(height));
  put32(sp+16,bits(radius));put32(sp+20,out);
  xctx c={0};
  for(unsigned i=0;i<8;i++){c.r[i]=0x12340000+i;c.st[i]=(double)i+.125;}
  c.r[0]=object;c.r[7]=q;c.r[4]=sp;c.preempt=10000;
  c.fsp=trial%8;c.fcw=0x37f;c.fsw=(uint16_t)(c.fsp<<11);
  c.f_kind=XK_SUB;c.f_op1=17;c.f_op2=3;c.f_res=14;c.f_bits=32;
  c.f_cf=1;c.f_of=1;
  xctx before=c;
  uint8_t stack_before[1024];memset(g_xram+sp-sizeof stack_before,0xa5,sizeof stack_before);
  memcpy(stack_before,g_xram+sp-sizeof stack_before,sizeof stack_before);
  memcpy(before_arena,g_xram,0x300000);
  started=ns_now();f_000868F0(&c);guest_ns+=ns_now()-started;assert(c.r[4]==sp+24);
  for(unsigned i=0;i<8;i++){
   if(c.r[i]!=before.r[i])register_mask|=1u<<i;
   if(memcmp(&c.st[i],&before.st[i],sizeof(double)))x87_slot_mask|=1u<<i;
  }
  assert(c.r[3]==before.r[3] && c.r[5]==before.r[5] && c.r[6]==before.r[6] && c.r[7]==before.r[7]);
  assert(c.fsp==before.fsp && c.fcw==before.fcw);
  assert(memcmp(c.xmm,before.xmm,sizeof c.xmm)==0 && memcmp(c.mm,before.mm,sizeof c.mm)==0);
  assert(c.r[1]==query.surface_count); /* pushed ECX is reused as a loop counter */
  assert(c.r[0]==(query.surface_count?query.surface_count:query.edge_count));
  changed_fsw+=c.fsw!=before.fsw;
  changed_flags+=memcmp(&c.f_kind,&before.f_kind,9*sizeof(uint32_t))!=0;
  unsigned used=(unsigned)(before.preempt-c.preempt);if(used>max_budget_used)max_budget_used=used;
  for(unsigned i=0;i<sizeof stack_before;i++)if(stack_before[i]!=g_xram[sp-sizeof stack_before+i]){
   unsigned depth=sizeof stack_before-i;if(depth>deepest_stack_change)deepest_stack_change=depth;
  }
  if(memcmp(actual,g_xram+out,sizeof actual)){
   fprintf(stderr,"query mismatch trial=%u\n",trial);abort();
  }
  memcpy(after_arena,g_xram,0x300000);memcpy(g_xram,before_arena,0x300000);
  query_route_test route={0};xctx routed=before;
  xk_feature_query_state_all_scoped(&routed,query_route_vertex,query_route_edge,query_route_surface,&route);
  assert(route.calls[0]==query.vertex_count&&route.calls[1]==query.edge_count&&route.calls[2]==query.surface_count);
  assert(!memcmp(&routed,&c,sizeof c)&&!memcmp(g_xram,after_arena,0x300000));
  /* Force scheduling callbacks, preserving the same budget/state trajectory.
   * This models resumption, not concurrent ownership or live scheduler safety. */
  if(trial<100){
   memcpy(g_xram,before_arena,0x300000);xctx yielded=before;yielded.preempt=1;
   preempt_test_enabled=1;preempt_test_calls=0;f_000868F0(&yielded);
   unsigned expected_yields=preempt_test_calls;
   uint8_t *yield_arena=malloc(0x300000);assert(yield_arena);memcpy(yield_arena,g_xram,0x300000);
   memcpy(g_xram,before_arena,0x300000);routed=before;routed.preempt=1;route=(query_route_test){0};
   preempt_test_calls=0;
   xk_feature_query_state_all_scoped(&routed,query_route_vertex,query_route_edge,query_route_surface,&route);
   assert(preempt_test_calls==expected_yields);
   assert(!memcmp(&routed,&yielded,sizeof routed)&&!memcmp(g_xram,yield_arena,0x300000));
   preempt_test_enabled=0;free(yield_arena);
  }
  memcpy(g_xram,before_arena,0x300000);
  xctx candidate=before;
  xk_feature_query_state(&candidate,vertex_state_candidate,f_000862A0,f_00086170);
  assert(memcmp(&candidate,&c,sizeof c)==0);
  assert(memcmp(g_xram,after_arena,0x300000)==0);
  memcpy(g_xram,before_arena,0x300000);candidate=before;
  int accepted=xk_feature_try_query_vertices(&candidate,0x300000,1<<20,f_000855F0,f_000862A0,f_00086170);
  assert(accepted==(query.vertex_count>=2&&(trial%5||height>0)));
  if(accepted)scoped_queries++;
  else{
   scoped_fallbacks++;
   assert(memcmp(&candidate,&before,sizeof before)==0);
   assert(memcmp(g_xram,before_arena,0x300000)==0);
   f_000868F0(&candidate);
  }
  assert(memcmp(&candidate,&c,sizeof c)==0);
  assert(memcmp(g_xram,after_arena,0x300000)==0);
 }
 puts("PASS scoped query routing: 1000 full-state cases and 100 forced-resumption cases");
 printf("PASS scoped mixed queries: %u native, %u unchanged-entry fallbacks\n",scoped_queries,scoped_fallbacks);
 free(before_arena);free(after_arena);
 assert(vertex_boundary_cases==256);
 puts("PASS vertex boundary: 256 immediate-return full context/arena comparisons");
 puts("PASS query state driver: 1000 full context/3 MiB arena comparisons with native point transform and vertex emitter");
 puts("PASS native query output: 1000 mixed/repeated-list differential cases");
 printf("Guest boundary footprint: registers=0x%x x87-slots=0x%x fsw-cases=%u flags-cases=%u deepest-changed-stack=%u max-backedges=%u\n",
        register_mask,x87_slot_mask,changed_fsw,changed_flags,deepest_stack_change,max_budget_used);
 printf("Synthetic query mean: native %.3f us, guest %.3f us (output-only, no runtime wrapper)\n",native_ns/1000000.0,guest_ns/1000000.0);

 /* Rejection must leave both guest context and every arena byte unchanged. */
 {
  uint8_t *saved=malloc(0x300000);assert(saved);
  unsigned rejection_cases=12;
#ifdef XV_CHECK_GUEST_ADDRESS
  rejection_cases=15;
#endif
  for(unsigned kind=0;kind<rejection_cases;kind++){
#ifdef XV_CHECK_GUEST_ADDRESS
   xv_watch_len=0;memset(xv_page_epoch,0,sizeof xv_page_epoch);
#endif
   memset(g_xram,0,0x300000);reference_init_constants();
   for(unsigned i=0;i<0x300;i++)g_xpt[i]=i*4096;
   xctx c={0};c.r[1]=out;c.r[4]=sp;c.r[6]=vertex;c.fcw=0x37f;
   uint64_t arena_bytes=0x300000;
   switch(kind){
    case 0:c.r[6]=out;break; /* output aliases point */
    case 1:c.r[1]=sp-8;break; /* output aliases scratch/args */
    case 2:g_xpt[vertex>>12]=out;break; /* physical, not virtual alias */
    case 3:g_xpt[(out>>12)+1]+=4096;break; /* split output */
    case 4:arena_bytes=out+16;break;
    case 5:c.r[4]=4;break;
    case 6:c.r[4]=UINT32_MAX-8;break;
    case 7:put32(0x1f0a68,1);break;
    case 8:put32(out,257);break;
    case 9:put32(sp+4,0x7fc00000);break;
    case 10:c.fcw=0;break;
    case 11:c.r[1]=UINT32_MAX-4;break;
#ifdef XV_CHECK_GUEST_ADDRESS
    case 12:g_xpt[vertex>>12]=xv_trash_off;break;
    case 13:xv_watch_off=out+8;xv_watch_len=4;break;
    case 14:c.r[6]=0xfd000000;break;
#endif
   }
   xctx before=c;memcpy(saved,g_xram,0x300000);
   assert(!xk_feature_try_vertex_emit(&c,arena_bytes,1<<20));
   assert(memcmp(&c,&before,sizeof c)==0);
   assert(memcmp(g_xram,saved,0x300000)==0);
#ifdef XV_CHECK_GUEST_ADDRESS
   for(unsigned page=0;page<XV_PAGE_EPOCH_ENTRIES;page++)assert(xv_page_epoch[page]==0);
#endif
  }
  free(saved);
 }
 puts("PASS vertex admission: rejection preserves context/arena; checked policy, watch and epoch checks when enabled");
 free(g_xpt);free(g_xram);printf("PASS feature reference: %u capacity/extrusion/transform/ownership cases\n",cases);
}
