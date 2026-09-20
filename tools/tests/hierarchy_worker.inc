#include <xmmintrin.h>
/* Full production hierarchy in real object workers. Poison captured pose
 * storage during suspension: any subsequent borrowed read corrupts the result. */
int xv_math_model_hierarchy(xctx *);
int __real_xv_object_hierarchy_suspend(xctx *,int,uint32_t,unsigned);
void __real_xv_object_hierarchy_resume(int);
static _Thread_local uint32_t hierarchy_poison;
static _Thread_local unsigned hierarchy_suspensions;
static _Thread_local unsigned char hierarchy_pose_saved[256];
int __wrap_xv_object_hierarchy_suspend(xctx *c,int guard,uint32_t out,unsigned bytes)
{
    int token=__real_xv_object_hierarchy_suspend(c,guard,out,bytes);
    if(token&&hierarchy_poison) {
        memcpy(hierarchy_pose_saved,X_G(hierarchy_poison),256);
        memset(X_G(hierarchy_poison),0xcd,256);hierarchy_suspensions++;
    }
    return token;
}
void __wrap_xv_object_hierarchy_resume(int token)
{
    if(token&&hierarchy_poison)memcpy(X_G(hierarchy_poison),hierarchy_pose_saved,256);
    __real_xv_object_hierarchy_resume(token);
}
static void compare_hierarchy(xctx *c,unsigned id)
{
    xctx entry=*c;
    uint32_t sp=(entry.r[4]&~4095u)-8192+64,model=sp+0x400,nodes=sp+0x500,out=sp+0xa00;
#ifdef TEST_HIERARCHY_SHARED
    out=0xa0000+id*512;
#endif
    uint32_t poses=0x60000+id*256;
    unsigned char initial_out[416],expected_out[416];
    unsigned char initial[3072],expected[3072],pose_initial[256];
    memset(X_G(sp),0xa5,sizeof initial);
    X_M32(model+0xb8)=8;X_M32(model+0xbc)=nodes;
    X_M32(sp+0x24)=out;X_M32(sp+0x28)=poses;X_M32(sp+0x2c)=model;
    X_M32(sp+0x10)=2;X_M16(sp+0x178)=0;X_M16(sp+0x17a)=1;
    for(unsigned n=0;n<8;n++) {
        X_M16(nodes+n*156+0x20)=n==7?0xffff:n+1;
        X_M16(nodes+n*156+0x22)=0xffff;
        X_M16(nodes+n*156+0x24)=n?n-1:0xffff;
        for(unsigned j=0;j<8;j++)X_MF32(poses+n*32+j*4)=j==7?1.f:(id&1)?10000.f:.125f;
        for(unsigned j=0;j<13;j++)X_MF32(out+n*52+j*4)=(id&1)?100000000.f:(j==0||j==1||j==5||j==9)?1.f:0.f;
    }
    c->r[0]=1;c->r[4]=sp;c->preempt=100;c->df=0;
    xctx original=*c,reference=*c;
    memcpy(initial,X_G(sp),sizeof initial);memcpy(pose_initial,X_G(poses),256);
    memcpy(initial_out,X_G(out),sizeof initial_out);
    unsigned saved_mode=_mm_getcsr();
#ifdef TEST_HIERARCHY_SHARED
    _mm_setcsr((saved_mode&~0x6000u)|((id%4u)<<13)|0x20u);
#endif
    unsigned fp=_mm_getcsr();
    int want=xv_math_model_hierarchy(&reference);unsigned expected_fp=_mm_getcsr();
    memcpy(expected_out,X_G(out),sizeof expected_out);
    memcpy(expected,X_G(sp),sizeof expected);
    memcpy(X_G(sp),initial,sizeof initial);_mm_setcsr(fp);
    memcpy(X_G(out),initial_out,sizeof initial_out);
    hierarchy_poison=poses;hierarchy_suspensions=0;
    int got=xv_math_model_hierarchy(c);
    hierarchy_poison=0;
    if(got!=want||got!=!(id&1))fprintf(stderr,"hierarchy id %u got %d want %d fp %x susp %u\n",id,got,want,fp,hierarchy_suspensions);
    assert(got==want&&got==!(id&1));
    assert(!memcmp(c,&reference,sizeof *c));
    assert(!memcmp(expected,X_G(sp),sizeof expected));
    assert(!memcmp(expected_out,X_G(out),sizeof expected_out));
    assert(!memcmp(pose_initial,X_G(poses),256));
    assert(_mm_getcsr()==expected_fp);
#ifdef TEST_HIERARCHY_SHARED
    assert(!hierarchy_suspensions);
#else
    assert(hierarchy_suspensions==(unsigned)(release_enabled&&fast_path&&workers!=0));
#endif
    if(id&1)assert(!memcmp(c,&original,sizeof *c)&&!memcmp(initial,X_G(sp),sizeof initial)&&_mm_getcsr()==fp);
    _mm_setcsr(saved_mode);*c=entry;
}
