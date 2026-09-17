#include "xv_x86rt.h"
#include <stddef.h>
#include <stdlib.h>

/* Synthetic solver packets. No captured game geometry or generated guest code. */
enum { ARENA=8<<20,PAGES=1024,PACKET=0x10000,INPUT=0x20000,
       OUTPUT=0x21000,SP=0x90000 };
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
static xctx context;
xctx *ns_current_context=&context;
xctx *const arm_context_ptr=&context;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),
    offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
unsigned ns_site,ns_yields,ns_events,ns_seen,ns_fallbacks,ns_traps,ns_scope_depth;
unsigned ns_mutation_site,ns_mutations;
uint32_t ns_original_pages[PAGES],ns_alternate_pages[PAGES];
static unsigned variant;
unsigned xv_object_hold_children_enabled;
void ns_original_call(xctx *);
void ns_candidate_call(xctx *);
void abort(void){__builtin_trap();}
char *getenv(const char *name){(void)name;return NULL;}
unsigned long strtoul(const char *s,char **end,int base)
{(void)s;(void)end;(void)base;abort();}
int snprintf(char *s,size_t n,const char *fmt,...)
{(void)s;(void)n;(void)fmt;abort();}
double sqrt(double v)
{double result;__asm__ volatile("vsqrt.f64 %P0, %P1":"=w"(result):"w"(v));return result;}
void x_guest_read_pages(void *out,uint32_t a,size_t n)
{unsigned char *p=out;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(p,X_G(a),k);p+=k;a+=k;n-=k;}}
void x_guest_write_pages(uint32_t a,const void *in,size_t n)
{const unsigned char *p=in;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(X_G(a),p,k);p+=k;a+=k;n-=k;}}
static void put(unsigned a,unsigned v){X_M32(a)=v;}
static void flt(unsigned a,float v){X_MF32(a)=v;}
static void hash(const void *p,unsigned n)
{const unsigned char *b=p;for(unsigned i=0;i<n;i++)ns_events=(ns_events^b[i])*16777619u;}
static void observe(xctx *c,unsigned pc)
{if(c!=ns_current_context)abort();hash(&pc,4);hash(c,sizeof *c);}
unsigned xv_object_motion_begin(xctx *c,unsigned site)
{if(site!=4||ns_scope_depth)abort();observe(c,0xff000004);ns_scope_depth++;return 1;}
void xv_object_motion_end(unsigned *token)
{if(*token){if(ns_scope_depth!=1)abort();observe(ns_current_context,0xff00ffff);ns_scope_depth--;*token=0;}}
void xv_trap(xctx *c,uint32_t address)
{observe(c,address);ns_traps++;}
#ifdef NS_TRACE
void __real_x_str_movs(xctx *,unsigned,int);
void __wrap_x_str_movs(xctx *c,unsigned size,int mode)
{observe(c,0xff010000u|size);__real_x_str_movs(c,size,mode);observe(c,0xff020000u|size);}
#endif
void xv_preempt(xctx *c)
{
    if(++ns_yields>200000)abort();
    observe(c,ns_site);
    const unsigned sites[]={0x85839,0x85847,0x859a0,0x859b3,0x859c2,0x85b63,0x85b70,
        0x85c63,0x85c76,0x8660a,0x8661d,0x1710b6,0x17124d,0x1713c9};
    for(unsigned i=0;i<sizeof sites/sizeof *sites;i++)if(ns_site==sites[i])ns_seen|=1u<<i;
    for(unsigned i=0;i<128;i++){unsigned char v=X_M8(c->r[4]+i);hash(&v,1);}
    c->preempt=variant&8?1:3; /* Observe every taken preempt site when requested. */
    if(ns_mutation_site ? ns_site!=ns_mutation_site||ns_mutations : ns_yields!=1)return;
    ns_mutations++;
    switch((variant>>4)&7){
    case 1:c->f_cf^=1;c->f_of^=1;c->fsw^=0x4100;break;
    case 2:c->fsp=(c->fsp+3)&7;c->st[c->fsp]=.75;break;
    case 3:memcpy(ns_alternate_pages,g_xpt,sizeof ns_alternate_pages);g_xpt=ns_alternate_pages;
        memcpy(g_xram+0x301000,g_xram+g_xpt[OUTPUT>>12],4096);g_xpt[OUTPUT>>12]=0x301000;
        break;
    case 4:flt(PACKET+8+28+12,2.f);break;
    case 5:memcpy(ns_alternate_pages,g_xpt,sizeof ns_alternate_pages);g_xpt=ns_alternate_pages;
        {unsigned page=c->r[4]>>12;memcpy(g_xram+0x302000,g_xram+g_xpt[page],4096);g_xpt[page]=0x302000;}
        break;
    case 6:memcpy(ns_alternate_pages,g_xpt,sizeof ns_alternate_pages);g_xpt=ns_alternate_pages;
        {unsigned page=(SP+28)>>12;memcpy(g_xram+0x303000,g_xram+g_xpt[page],4096);g_xpt[page]=0x303000;}
        X_M32(SP+28)^=0x01010101u; /* Real caller POP must use the current root. */
        break;
    case 7:if(ns_site!=0x8660a)abort();
        memcpy(ns_alternate_pages,g_xpt,sizeof ns_alternate_pages);g_xpt=ns_alternate_pages;
        {unsigned page=c->r[4]>>12;memcpy(g_xram+0x304000,g_xram+g_xpt[page],4096);g_xpt[page]=0x304000;}
        X_M32(c->r[4]+8)^=1; /* 864C0's saved EBP, later popped using global roots. */
        break;
    }
}
void arm_prepare(unsigned count,unsigned packed,unsigned budget)
{
    unsigned kind=packed&255;variant=packed>>8;
    for(unsigned i=0;i<PAGES;i++)ns_original_pages[i]=(i^1)*4096;
    memset(ns_alternate_pages,0,sizeof ns_alternate_pages);g_xpt=ns_original_pages;
    memset(g_xram,0x33,ARENA);g_img_base=g_xram+(4<<20);
    memset(&context,0xa5,sizeof context);
    const unsigned pairs[][2]={{0x1f0a68,0},{0x1f0a78,0x3f800000},
        {0x1f0af8,0xe0000000},{0x1f0afc,0x3f1a36e2},{0x1f0c24,0xb8d1b717},
        {0x206f9c,0x1eaebc},{0x1eaebc,0},{0x1eaec0,0},{0x1eaec4,0x3f800000},
        {0x1eaf30,0x10002},{0x1eaf34,0x20001},{0x1eaf38,0x20000},
        {0x1eaf3c,2},{0x1eaf40,1},{0x1eaf44,0x10000}};
    for(unsigned i=0;i<sizeof pairs/sizeof *pairs;i++){
        put(pairs[i][0],pairs[i][1]);memcpy(g_img_base+pairs[i][0],&pairs[i][1],4);
    }
    /* Clear through guest mappings, not a flat pointer. */
    for(unsigned i=0;i<0xac08;i++)X_M8(PACKET+i)=0;
    X_M16(PACKET)=kind==1||kind==4?count:0;
    X_M16(PACKET+2)=kind==2||kind==4?count:0;
    X_M16(PACKET+4)=kind==3||kind==4?count:0;
    for(unsigned i=0;i<count;i++){
        unsigned s=PACKET+8+28*i;
        put(s,i);put(s+4,0x11110000+i);put(s+8,0x22220000+i);
        flt(s+12,i%2?10.f:0.f);flt(s+16,0.f);flt(s+20,0.f);flt(s+24,.25f);
        s=PACKET+0x1c08+40*i;
        put(s,i);put(s+4,0x33330000+i);put(s+8,0x44440000+i);
        flt(s+12,i%2?10.f:0.f);flt(s+16,0);flt(s+20,0);
        flt(s+24,0);flt(s+28,0);flt(s+32,1);flt(s+36,.25f);
        s=PACKET+0x4408+104*i;
        put(s,i);put(s+4,0x55550000+i);put(s+8,0x66660000+i);
        flt(s+12,0);flt(s+16,0);flt(s+20,1);flt(s+24,i%2?10.f:0.f);flt(s+28,0);
        X_M16(s+32)=2;X_M8(s+34)=variant&1;put(s+36,4);
        const float points[8]={-10,-10,10,-10,10,10,-10,10};
        for(unsigned j=0;j<8;j++)flt(s+40+4*j,points[j]);
    }
    flt(INPUT,0);flt(INPUT+4,0);flt(INPUT+8,1);
    flt(INPUT+16,variant&2?0:.2f);flt(INPUT+20,variant&2?0:-.15f);flt(INPUT+24,variant&2?0:-2.f);
    for(unsigned i=0;i<8;i++)context.r[i]=0x31310000+17*i;
    /* Entry is the original CALL at 172CB8, before it pushes its return word. */
    context.r[4]=SP+4;context.r[0]=INPUT;
    put(SP+4,INPUT+16);put(SP+8,PACKET);put(SP+12,OUTPUT);put(SP+16,OUTPUT+16);
    put(SP+20,16);put(SP+24,OUTPUT+32);put(SP+28,0x41424344);
    context.fsp=(variant>>8)&7;context.fcw=0x027f;context.fsw=0x3210;
    for(unsigned i=0;i<8;i++)context.st[i]=(double)i*.125;
    unsigned exceptional=(variant>>12)&7;
    if(exceptional){
        const uint32_t words[]={0,0x7fc12345,0x7f812345,0x7f800000,0x80000000,1,0xffc54321,0xff800000};
        put(INPUT+16,words[exceptional]);
        uint64_t stale=0x7ff8123456789abcULL;
        for(unsigned i=0;i<8;i++){uint64_t v=stale+i;memcpy(&context.st[i],&v,8);}
    }
    context.df=0;context.f_kind=XK_SUB;context.f_bits=32;context.f_op1=2;context.f_op2=1;
    context.f_res=1;context.f_cf_override=0;context.f_of_override=0;context.preempt=budget;
    ns_current_context=&context;ns_site=ns_yields=ns_events=ns_seen=ns_fallbacks=ns_traps=ns_scope_depth=0;
    ns_mutation_site=ns_mutations=0;
    xv_object_hold_children_enabled=!!(variant&4);
}
void arm_original(void){ns_original_call(&context);}
void arm_candidate(void){ns_candidate_call(&context);}
void test_boot(void){}
