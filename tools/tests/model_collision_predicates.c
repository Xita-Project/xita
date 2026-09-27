#include "xv_x86rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
__thread uint32_t *xv_host_page_table;
void reference(xctx*);void candidate(xctx*);
#define SIZE (1u<<20)
static unsigned scenario,calls,preempts,covered[4],seed=7;
static uint64_t trace;
static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}
static void hash(const void *v,size_t n){const unsigned char*p=v;while(n--)trace=(trace^*p++)*1099511628211ull;}
void x_guest_read_pages(void *out,uint32_t a,size_t n){unsigned char*p=out;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(p,X_G(a),k);p+=k;a+=k;n-=k;}}
void x_guest_write_pages(uint32_t a,const void *in,size_t n){const unsigned char*p=in;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(X_G(a),p,k);p+=k;a+=k;n-=k;}}
void xv_preempt(xctx*c){preempts++;c->preempt=3;}
static void mock(xctx*c,unsigned id){
 unsigned sp=c->r[4],out=id==3?c->r[1]:c->r[0];
 hash(&id,4);hash(c,sizeof *c);unsigned char args[24];x_guest_read(args,sp,24);hash(args,24);calls++;covered[id]++;
 for(unsigned j=0;j<(id==0?52:id==3?32:12);j+=4)X_M32(out+j)=scenario*3+j+id;
 c->r[0]=(scenario+calls)&1;c->r[1]=0x80011200;c->r[2]=0x80011400;
 c->f_kind=XK_SUB;c->f_op1=scenario;c->f_op2=calls;c->f_res=scenario-calls;c->f_bits=32;
 c->f_cf_override=1;c->f_cf=calls&1;c->f_of_override=1;c->f_of=scenario&1;
 for(unsigned j=0;j<8;j++)c->st[j]=(scenario+j)*0.125;
 c->fsw=(uint16_t)(scenario*17);c->r[4]=sp+(id==3?24:4);
}
void f_000B6210(xctx*c){mock(c,0);}void f_000B5EA0(xctx*c){mock(c,1);}
void f_000B5E40(xctx*c){mock(c,2);}void f_00088E90(xctx*c){mock(c,3);}
int main(int argc,char**argv){
 unsigned n=argc>1?strtoul(argv[1],0,0):1024,total=0;
 g_xram=calloc(1,SIZE);g_img_base=g_xram;g_xpt=malloc((1u<<20)*4);xv_host_page_table=g_xpt;
 unsigned char*before=malloc(SIZE),*expected=malloc(SIZE);assert(g_xram&&g_xpt&&before&&expected);
 for(unsigned i=0;i<(1u<<20);i++)g_xpt[i]=((i*17)&255)*4096;
 for(scenario=0;scenario<n;scenario++){
 memset(g_xram,0,SIZE);xctx init={0};for(unsigned j=0;j<8;j++){init.r[j]=rnd();init.st[j]=(int32_t)rnd()*0.125;}
 init.r[4]=0x800ee000;init.fsp=scenario&7;init.fcw=0x37f;init.fsw=rnd();init.preempt=scenario&1?0:100;
 init.f_kind=XK_SUB;init.f_op1=rnd();init.f_op2=rnd();init.f_res=rnd();init.f_bits=32;
 init.f_cf_override=scenario&1;init.f_of_override=(scenario>>1)&1;init.f_cf=1;init.f_of=1;
 unsigned instance=0x80011000,model=0x80012000,nodes=0x80013000,perms=0x80014000,out=0x80015000;
 unsigned args[]={0x12345678,instance,scenario,0x80018000,0x80019000,out};x_guest_write(init.r[4],args,sizeof args);
 X_M32(instance+4)=model;X_M32(instance+8)=perms;X_M32(instance+12)=0x80016000;
 X_M32(model+0x28c)=scenario%13==0?0xffffffffu:scenario%9;X_M32(model+0x290)=nodes;
 for(unsigned j=0;j<8;j++){
 unsigned node=nodes+j*64,bsp=0x80020000+j*0x1000;
 X_M16(node+32)=(scenario+j)%5==0?0xffffu:j%4;
 X_M8(perms+j%4)=(scenario+j)%256;
 X_M32(node+52)=(scenario+j)%7==0?0xffffffffu:(scenario+j)%4;X_M32(node+56)=bsp;
 for(unsigned k=0;k<4;k++)X_M32(bsp+k*96)=(scenario+j+k)%3==0?0:5;
 }
 memcpy(before,g_xram,SIZE);xctx ref=init;calls=preempts=0;trace=1469598103934665603ull;reference(&ref);
 unsigned rc=calls,rp=preempts;uint64_t rt=trace;memcpy(expected,g_xram,SIZE);memcpy(g_xram,before,SIZE);
 xctx got=init;calls=preempts=0;trace=1469598103934665603ull;candidate(&got);
 if(memcmp(&ref,&got,sizeof ref)||memcmp(expected,g_xram,SIZE)||rc!=calls||rp!=preempts||rt!=trace){fprintf(stderr,"mismatch %u ctx%d mem%d calls%u/%u\n",scenario,memcmp(&ref,&got,sizeof ref)!=0,memcmp(expected,g_xram,SIZE)!=0,rc,calls);return 1;}total+=calls;
 }
 for(unsigned j=0;j<4;j++)assert(covered[j]);printf("PASS %u cases, %u candidate callee observations; full context/arena/trace/preemption matched\n",n,total);
 free(expected);free(before);free(g_xpt);free(g_xram);return 0;
}
