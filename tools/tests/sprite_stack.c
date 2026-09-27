#include "xv_x86rt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
enum { SIZE=8<<20, PAGES=2048 };
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
static unsigned mode,yields,calls; static xctx trace[16];
void original(xctx*);void candidate(xctx*);
void x_guest_read_pages(void *out,uint32_t a,size_t n){uint8_t *p=out;while(n--)*p++=X_M8(a++);}
void x_guest_write_pages(uint32_t a,const void *in,size_t n){const uint8_t *p=in;while(n--)X_M8(a++)=*p++;}
static void put(uint32_t a,uint32_t v){x_guest_write_pages(a,&v,4);}
static void fp(uint32_t a,float f){x_guest_write_pages(a,&f,4);}
void xv_preempt(xctx *c){
 assert(yields+calls<16);trace[yields+calls]=*c;yields++;c->preempt=1;
 if(yields!=1)return;
 if(mode==1){unsigned page=c->r[4]>>12;memcpy(g_xram+0x30000,g_xram+g_xpt[page],4096);g_xpt[page]=0x30000;}
 if(mode==2){uint8_t b[256];x_guest_read_pages(b,c->r[4],256);c->r[4]=0x18000;x_guest_write_pages(c->r[4],b,256);}
 if(mode==3){c->scratch^=0x12345678;fp(c->r[4]+0x24,-0.25f);}
}
void f_0005BA10(xctx *c){
 assert(yields+calls<16);trace[yields+calls]=*c;calls++;
 /* Model a nontrivial callee: observe input, write memory and context. */
 put(0x40000,c->r[1]);put(0x40004,X_M32(c->r[1]));
 c->r[0]=0x12345678;c->st[(c->fsp+7)&7]=0.75;c->r[4]+=4;
}
int main(void){
 g_xram=malloc(SIZE);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
 uint8_t *before=malloc(SIZE),*want=malloc(SIZE);uint32_t pages[PAGES];xctx seen[16];unsigned total_yields=0,total_calls=0;
 for(unsigned k=0;k<2048;k++){
  for(unsigned j=0;j<PAGES;j++)g_xpt[j]=(j^1)*4096;
  memset(g_xram,0,SIZE);xctx c;memset(&c,0xa5,sizeof c);
  uint32_t sp=(uint32_t[]){0x6000,0x6f90,0x6001,0x6f91}[(k/64)%4];
  c.r[0]=0x9000;c.r[1]=0xff00aabb;c.r[2]=0x3f000000;c.r[3]=(k/8)%4;c.r[4]=sp;c.r[5]=0xb000;c.r[6]=(k/128)%2;c.r[7]=(k/256)%2;
  c.fsp=k%8;c.fsw=0x1200;c.fcw=0x37f;c.preempt=(k/256)%2?1:100;for(unsigned j=0;j<8;j++)c.st[j]=(double)j/7;
  for(unsigned off=0;off<256;off+=4)fp(sp+off,((int)((off+k)%19)-9)/8.0f);
  for(unsigned off=0;off<32;off+=4)fp(0x9000+off,((int)((off+k)%13)-6)/4.0f);
  put(sp+0x1c,0);put(sp+0x28,0x9000);put(sp+0x30,0xa000);
  put(0xa004,((k/192)%2)?sp+0x4c:0x20000);X_M8(0xb010)=(k/32)%2;
  fp(0xb014,0.1f);fp(0xb018,0.2f);fp(0xb01c,0.3f);fp(0x1f0aa0,0.5f);
  mode=(k/512)%4;xctx initial=c;memcpy(before,g_xram,SIZE);yields=calls=0;original(&c);
  xctx expected=c;unsigned ny=yields,nc=calls;memcpy(seen,trace,(ny+nc)*sizeof(xctx));memcpy(want,g_xram,SIZE);memcpy(pages,g_xpt,sizeof pages);
  for(unsigned j=0;j<PAGES;j++)g_xpt[j]=(j^1)*4096;
  memcpy(g_xram,before,SIZE);c=initial;yields=calls=0;candidate(&c);
  int ctx=memcmp(&c,&expected,sizeof c),mem=memcmp(g_xram,want,SIZE),pt=memcmp(g_xpt,pages,sizeof pages),tr=(ny!=yields||nc!=calls||memcmp(seen,trace,(ny+nc)*sizeof(xctx)));
  if(ctx||mem||pt||tr){fprintf(stderr,"FAIL %u ctx %d mem %d pages %d trace %d\n",k,ctx,mem,pt,tr);return 1;}
  total_yields+=ny;total_calls+=nc;
 }
 printf("PASS 2048 full-context/arena/mapping/callee/handoff cases; %u yields %u callees\n",total_yields,total_calls);
 free(before);free(want);free(g_xpt);free(g_xram);return 0;
}
