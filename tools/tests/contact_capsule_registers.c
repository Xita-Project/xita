/* Synthetic 862A0 lowering validation, not collision correctness: strict full
 * context, arena, call observations and preemption counts with stub callees. */
#include "xv_x86rt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
__thread uint32_t *xv_host_page_table;
#endif
void contact_capsule_reference(xctx*);void contact_capsule_candidate(xctx*);
#define SIZE (4u<<20)
static unsigned scenario,steps,calls,preempts,covered[4],mode,boundaries;
static xctx boundary_state[64];
static void boundary(xctx *c,unsigned id){assert(boundaries<64);if(!mode)boundary_state[boundaries]=*c;else if(memcmp(c,&boundary_state[boundaries],sizeof *c)){
 fprintf(stderr,"boundary mismatch scenario=%u event=%u id=%x\n",scenario,boundaries,id);
 for(unsigned i=0;i<sizeof *c;i++)if(((unsigned char*)c)[i]!=((unsigned char*)&boundary_state[boundaries])[i])fprintf(stderr," offset %u ref=%02x candidate=%02x\n",i,((unsigned char*)&boundary_state[boundaries])[i],((unsigned char*)c)[i]);
 }boundaries++;}
static uint64_t trace;
static void hash(const void *p,size_t n){const unsigned char *s=p;while(n--)trace=(trace^*s++)*1099511628211ull;}
void contact_capsule_step(unsigned ip){if(++steps>10000){fprintf(stderr,"loop %x\n",ip);abort();}}
void x_guest_read_pages(void *o,uint32_t a,size_t n){unsigned char *p=o;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(p,X_G(a),k);a+=k;p+=k;n-=k;}}
void x_guest_write_pages(uint32_t a,const void *i,size_t n){const unsigned char *p=i;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(X_G(a),p,k);a+=k;p+=k;n-=k;}}
void xv_preempt(xctx *c){preempts++;boundary(c,0xffffffffu);hash(c,sizeof *c);if(scenario&256){for(unsigned i=0;i<8;i++)c->st[i]=(double)(i+1)*0.375;c->fsw^=0x4500;}c->preempt=3;}
void xv_trap(xctx*c,uint32_t ip){(void)c;fprintf(stderr,"trap %x\n",ip);abort();}
static void wf(uint32_t a,float v){x_guest_write(a,&v,4);}
static void observe(xctx*c,unsigned id){calls++;covered[id]++;boundary(c,id);hash(&id,sizeof id);hash(c,sizeof *c);hash(g_xram,SIZE);
 for(unsigned i=0;i<8;i++)c->st[i]=(double)(scenario+i+1)*0.03125;
 c->fsw=(uint16_t)(scenario*37);c->f_kind=XK_LOGIC;c->f_bits=32;c->f_res=scenario&1;c->f_op1=c->f_op2=0;
}
void f_000116F0(xctx*c){observe(c,0);x87_push(c,(scenario&16)?-1.0:1.0);c->r[4]+=4;}
void f_000B5E40(xctx*c){uint32_t out=c->r[0];observe(c,1);for(unsigned i=0;i<3;i++)wf(out+i*4,(float)(scenario%13+i)*0.125f);c->r[0]=out;c->r[4]+=4;}
void f_000B5EA0(xctx*c){uint32_t out=c->r[0];observe(c,2);for(unsigned i=0;i<3;i++)wf(out+i*4,(float)(scenario%19+i)*0.0625f);c->r[0]=out;c->r[4]+=4;}
void f_000851E0(xctx*c){observe(c,3);c->r[0]=scenario;c->r[4]+=32;}
int main(int argc,char **argv){unsigned count=argc>1?strtoul(argv[1],0,0):256;
 g_xram=calloc(1,SIZE);g_img_base=g_xram;g_xpt=malloc((1u<<20)*4);uint8_t *before=malloc(SIZE),*expected=malloc(SIZE);assert(g_xram&&g_xpt&&before&&expected);
 for(unsigned i=0;i<(1u<<20);i++)g_xpt[i]=(i&1023u)*4096;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
 xv_host_page_table=g_xpt;
#endif
 for(scenario=0;scenario<count;scenario++){memset(g_xram,0,SIZE);xctx initial={0};uint32_t sp=0xd003e000,desc=0x40010000,matrix=0x40012000;
 initial.r[4]=sp;initial.r[0]=0;initial.r[1]=desc;initial.fsp=(scenario>>2)&7;initial.fcw=0x37f;initial.fsw=scenario;initial.preempt=(scenario&2)?1:100;
 for(unsigned i=0;i<8;i++)initial.st[i]=(double)(i+scenario)*0.125;
 uint32_t surfaces=0x40010ff8,edges=0x40014000,vertices=0x40016000,planes=0x40018000;
 X_M32(desc+0x40)=surfaces;X_M32(desc+0x4c)=edges;X_M32(desc+0x58)=vertices;X_M32(desc+0x10)=planes;
 X_M32(edges)=0;X_M32(edges+4)=1;X_M32(edges+16)=0;X_M32(edges+20)=1;
 X_M32(surfaces)=0;
 X_M32(surfaces+12)=scenario%4==0?0:scenario%4==1?0x80000000u:scenario%4==2?0x80000001u:1;
 X_M32(sp+4)=(scenario&32)?matrix:0;X_M32(sp+16)=(scenario&64)?0xffffffffu:7;
 wf(planes,1);wf(planes+20,1);wf(vertices+24,(scenario&128)?-1:1);
 wf(0x1f0c24,0);wf(0x1f0b08,0);
 for(unsigned i=0;i<13;i++)wf(matrix+4*i,(float)((int)(scenario%7)-3+i)*0.125f);
 memcpy(before,g_xram,SIZE);xctx ref=initial;steps=calls=preempts=0;trace=0;mode=0;boundaries=0;contact_capsule_reference(&ref);uint64_t rt=trace;unsigned rc=calls,rp=preempts;memcpy(expected,g_xram,SIZE);
 memcpy(g_xram,before,SIZE);xctx cand=initial;steps=calls=preempts=0;trace=0;mode=1;boundaries=0;contact_capsule_candidate(&cand);
 if(memcmp(&ref,&cand,sizeof ref)||memcmp(expected,g_xram,SIZE)||trace!=rt||calls!=rc||preempts!=rp){fprintf(stderr,"mismatch scenario=%u context=%d memory=%d trace=%d\n",scenario,memcmp(&ref,&cand,sizeof ref),memcmp(expected,g_xram,SIZE),trace!=rt);free(before);free(expected);free(g_xpt);free(g_xram);return 1;}}
 for(unsigned i=0;i<4;i++)assert(covered[i]);printf("PASS %u capsule cases: full context/memory/callee trace/preemption; synthetic callees\n",count);free(before);free(expected);free(g_xpt);free(g_xram);return 0;}
