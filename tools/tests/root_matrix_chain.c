#define _POSIX_C_SOURCE 200809L
#include "xv_x86rt.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <fenv.h>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
int xv_phase_enabled;
void xk_os_log(const char *fmt,...){(void)fmt;}
int xv_math_matrix_multiply(xctx *);
int xv_math_root_chain(xctx *);
static unsigned raw_fp(void) {
#if defined(__arm__)
 unsigned v;__asm__ volatile("vmrs %0, fpscr":"=r"(v)::"memory");return v;
#elif defined(__x86_64__)
 return _mm_getcsr();
#else
 return (unsigned)fetestexcept(FE_ALL_EXCEPT);
#endif
}
static void fp_mode(unsigned k) {
#if defined(__arm__)
 unsigned v=(raw_fp()&~0x0300009fu)|(((k>>2)&3u)<<24);
 __asm__ volatile("vmsr fpscr, %0"::"r"(v):"memory");
#else
 (void)k;
#endif
}
static uint32_t rng=578231;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void original_pair(xctx *c){
 assert(xv_math_matrix_multiply(c));
 c->r[6]=X_M32(c->r[4]+0x1cu);
 assert(!c->r[6]);X_FLAGS(XK_LOGIC,0,0,0,32);
 c->r[6]=X_M32(c->r[4]+0x24u);
 X_PUSH32(c->r[6]);c->r[1]=c->r[4]+0x44u;
 X_PUSH32(c->r[1]);c->r[2]=c->r[4]+0x7cu;
 X_PUSH32(c->r[2]);X_PUSH32(0x8e525u);
 assert(xv_math_matrix_multiply(c));
 X_PUSH32(c->r[6]);c->r[0]=c->r[4]+0x114u;
 X_PUSH32(c->r[0]);X_PUSH32(c->r[6]);X_PUSH32(0x8e58bu);
 assert(xv_math_matrix_multiply(c));
}
int main(int argc,char **argv){
 int decline=argc<=1||strcmp(argv[1],"enabled");
 if(argc>1&&!strcmp(argv[1],"enabled"))setenv("XV_ROOT_CHAIN","1",1);
 else if(argc>1&&!strcmp(argv[1],"phase")){setenv("XV_ROOT_CHAIN","1",1);xv_phase_enabled=1;}
 else if(argc>1&&!strcmp(argv[1],"scene")){setenv("XV_ROOT_CHAIN","1",1);setenv("XV_SCENE_PHASES","1",1);}
 else if(argc>1&&!strcmp(argv[1],"math-off")){setenv("XV_ROOT_CHAIN","1",1);setenv("XV_NATIVE_MATH","0",1);}
 else unsetenv("XV_ROOT_CHAIN");
 enum{N=1<<20};g_xram=malloc(N);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
 unsigned char *before=malloc(N),*expected=malloc(N);assert(g_xram&&g_xpt&&before&&expected);
 for(unsigned i=0;i<N/4096;i++)g_xpt[i]=i*4096;
 const int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
 const uint32_t edge[]={0,0x80000000,1,0x807fffff,0x7f800000,0xff800000,0x7fc01234,0x7f801234,0x3f800000};
 for(unsigned k=0;k<1024;k++){
  memset(g_xram,0xa5,N);xctx init={0};for(unsigned i=0;i<8;i++){init.r[i]=next();init.st[i]=i+.375;}
  init.fsp=k&7;init.fcw=0x37f;init.preempt=100;init.r[4]=0x41800;init.r[6]=0x51000;
  uint32_t sp=init.r[4],args[]={0x8e41e,sp+0x50,sp+0xec,sp+0x50};x_guest_write(sp,args,16);
  X_M32(sp+0x2c)=0;X_M32(sp+0x34)=0x51000;
  init.f_kind=XK_SUB;init.f_op1=99;init.f_op2=10;init.f_res=89;init.f_bits=8;
  init.f_cf=1;init.f_of=1;init.f_cf_override=1;init.f_of_override=1;
  for(unsigned m=0;m<4;m++)for(unsigned i=0;i<13;i++){
   uint32_t v=next();v=k%3==0?edge[(k+i+m)%9]:(v&0x807fffffu)|((110+k%30)<<23);
   X_M32((m==0?sp+0x50:m==1?sp+0xec:m==2?sp+0x84:sp+0x120)+i*4)=v;
  }
  assert(!fesetround(rounds[k%4]));assert(!feclearexcept(FE_ALL_EXCEPT));fp_mode(k);unsigned initial_fp=raw_fp();fenv_t fp;assert(!fegetenv(&fp));
  memcpy(before,g_xram,N);xctx reference=init,candidate=init;
  if(decline){assert(!xv_math_root_chain(&candidate));assert(!memcmp(before,g_xram,N));assert(!memcmp(&candidate,&init,sizeof init));assert(raw_fp()==initial_fp);continue;}
  original_pair(&reference);unsigned flags=raw_fp();
  memcpy(expected,g_xram,N);memcpy(g_xram,before,N);assert(!fesetenv(&fp));assert(xv_math_root_chain(&candidate));
  if(memcmp(expected,g_xram,N)||memcmp(&reference,&candidate,sizeof reference)||flags!=raw_fp()){
   for(unsigned z=0;z<8;z++)fprintf(stderr,"r%u %08x/%08x\n",z,reference.r[z],candidate.r[z]);
   for(unsigned z=0;z<N;z++)if(expected[z]!=g_xram[z]){fprintf(stderr,"first memory diff %x %02x/%02x\n",z,expected[z],g_xram[z]);break;}
   fprintf(stderr,"FAIL case %u memory=%d ctx=%d FP=%d/%d\n",k,memcmp(expected,g_xram,N),memcmp(&reference,&candidate,sizeof reference),flags,raw_fp());return 1;
  }
 }
 if(decline){free(before);free(expected);free(g_xram);free(g_xpt);puts("PASS 1024 disabled/diagnostic mutation-free declines");return 0;}
 for(unsigned k=0;k<15;k++){
  memcpy(g_xram,before,N);xctx init={0};init.r[4]=0x41800;init.r[6]=0x51000;
  switch(k){
   case 0:init.df=1;break;
   case 1:X_M32(0x41800)=0;break;
   case 2:X_M32(0x41804)=0x41880;break;
   case 3:X_M32(0x41808)=0x41854;break;
   case 4:X_M32(0x4182c)=0x52000;break;
   case 5:X_M32(0x41834)=0x41884;break;
   case 6:X_M32(0x41834)=0x51ffc;break;
   case 7:init.r[4]=UINT32_MAX-4;break;
   case 8:X_M32(0x41834)=0x51890;g_xpt[0x51]=0x41000;break;
   case 9:X_M32(0x4180c)=0x52000;break;
   case 10:X_M32(0x41834)=0x51002;break;
   case 11:init.r[4]=0x41f00;break;
   case 12:init.r[4]=8;break;
   case 13:init.r[4]=UINT32_MAX-0x100;break;
   case 14:init.r[4]=0x41ff0;g_xpt[0x42]=0x43000;break;
  }
  memcpy(expected,g_xram,N);xctx candidate=init;feclearexcept(FE_ALL_EXCEPT);
  assert(!xv_math_root_chain(&candidate));assert(!memcmp(expected,g_xram,N));
  assert(!memcmp(&candidate,&init,sizeof init));assert(!fetestexcept(FE_ALL_EXCEPT));g_xpt[0x51]=0x51000;g_xpt[0x42]=0x42000;
 }
 free(before);free(expected);free(g_xram);free(g_xpt);puts("PASS 1024 full-context/arena/FP native-chain cases; 15 mutation-free declines");return 0;
}
