/* Included by the real worker-pool fixture. This tests scope ownership and
 * admission, not Halo's surrounding object-state independence. */
#include "kernel/xk_object_solver.h"
static unsigned solver_entered,solver_peak,solver_running;
#ifdef OBJECT_SOLVER_TRANSLATED
void f_00170C10(xctx *);
void f_solver_hooked(xctx *);
#endif
static void solver_fixture_constants(void)
{
    const uint32_t pairs[][2]={
        {0x1f0a68,0},{0x1f0a78,0x3f800000},{0x1f0af8,0xe0000000},
        {0x1f0afc,0x3f1a36e2},{0x1f0c24,0xb8d1b717},
        {0x206f9c,0x1eaebc},{0x1eaebc,0},{0x1eaec0,0},{0x1eaec4,0x3f800000},
        {0x1eaf30,0x10002},{0x1eaf34,0x20001},{0x1eaf38,0x20000},
        {0x1eaf3c,2},{0x1eaf40,1},{0x1eaf44,0x10000}
    };
    for(unsigned i=0;i<sizeof pairs/sizeof *pairs;i++)X_M32(pairs[i][0])=pairs[i][1];
    if(getenv("OBJECT_SOLVER_TEST_BAD_CONSTANT"))X_M32(0x1f0c24)=0;
}
static void solver_fixture_scope(xctx *c)
{
    xctx saved=*c;
    uint32_t sp=c->r[4]-65536,packet=sp+64,inputs=packet+0xac08;
    memset(X_G(sp),0,65536);
    c->r[4]=sp;c->r[0]=inputs;c->df=0;
    X_M32(sp)=0x172cbd;X_M32(sp+4)=inputs+16;X_M32(sp+8)=packet;
    X_M32(sp+12)=inputs+32;X_M32(sp+16)=inputs+48;
    X_M32(sp+20)=16;X_M32(sp+24)=inputs+64;
    assert(!xv_object_solver_begin(c)); /* No enclosing transaction. */
    {
        XV_OBJECT_MATH_GUARD();
        int expected=xv_object_math_locked_>=2&&!getenv("OBJECT_HOLD_TEST")&&
            !getenv("OBJECT_SOLVER_TEST_OFF")&&!getenv("OBJECT_SOLVER_TEST_BAD_CONSTANT");
        xctx other=*c;
        assert(!xv_object_solver_begin(&other)); /* A borrowed/copy context. */
        { XV_OBJECT_MATH_GUARD();assert(!xv_object_solver_begin(c)); }
        X_M32(sp)=0;assert(!xv_object_solver_begin(c));X_M32(sp)=0x172cbd;
        X_M32(sp+20)=0;assert(!xv_object_solver_begin(c));X_M32(sp+20)=16;
        c->df=1;assert(!xv_object_solver_begin(c));c->df=0;
        int budget=c->preempt;c->preempt=65535;
        assert(!xv_object_solver_begin(c));c->preempt=budget;
        /* Shared output and in-stack aliasing both retain the outer lock. */
        X_M32(sp+12)=0x60000;assert(!xv_object_solver_begin(c));
        X_M32(sp+12)=packet;assert(!xv_object_solver_begin(c));X_M32(sp+12)=inputs+32;
        c->r[0]=0x50000;assert(!xv_object_solver_begin(c));c->r[0]=inputs;
        unsigned page=(packet+4096)>>12,physical=g_xpt[page];
        g_xpt[page]=0;assert(!xv_object_solver_begin(c));g_xpt[page]=physical;
        for(unsigned k=0;k<3;k++) {
            X_M16(packet+2*k)=257;assert(!xv_object_solver_begin(c));
            X_M16(packet+2*k)=0xffff;assert(!xv_object_solver_begin(c));X_M16(packet+2*k)=0;
        }
        X_M16(packet+4)=1;
        X_M16(packet+0x4408+0x20)=3;assert(!xv_object_solver_begin(c));
        X_M16(packet+0x4408+0x20)=0;
        X_M8(packet+0x4408+0x22)=2;assert(!xv_object_solver_begin(c));
        X_M8(packet+0x4408+0x22)=0;
        X_M32(packet+0x4408+0x24)=9;assert(!xv_object_solver_begin(c));
        X_M32(packet+0x4408+0x24)=8;
#ifdef OBJECT_SOLVER_TRANSLATED
        /* The same original instruction body runs first under the guard and
         * then in the admitted window. Cover empty, sphere, capsule, polygon,
         * mixed and capacity packets, with moving and stationary inputs. */
        unsigned id=saved.r[1],kind=id%6;
        X_M16(packet)=kind==1||kind==4||kind==5?(kind==5?256:1):0;
        X_M16(packet+2)=kind==2||kind==4||kind==5?(kind==5?256:1):0;
        X_M16(packet+4)=kind==3||kind==4||kind==5?(kind==5?256:1):0;
        X_MF32(inputs)=(float)(id%7)*0.15f;
        X_MF32(inputs+4)=(float)(id%5)*0.11f;
        X_MF32(inputs+8)=1.0f;
        X_MF32(inputs+16)=id%4?0.2f:0;
        X_MF32(inputs+20)=id%4?-0.15f:0;
        X_MF32(inputs+24)=id%4?-2.0f:0;
        for(unsigned i=0;i<X_M16(packet);i++) {
            uint32_t s=packet+8+28*i;
            X_MF32(s+12)=(float)(i%8)*0.3f;X_MF32(s+16)=0;
            X_MF32(s+20)=0;X_MF32(s+24)=0.25f;
        }
        for(unsigned i=0;i<X_M16(packet+2);i++) {
            uint32_t s=packet+0x1c08+40*i;
            X_MF32(s+12)=(float)(i%8)*0.3f;X_MF32(s+16)=0;X_MF32(s+20)=0;
            X_MF32(s+24)=0;X_MF32(s+28)=0;X_MF32(s+32)=1;X_MF32(s+36)=0.25f;
        }
        for(unsigned i=0;i<X_M16(packet+4);i++) {
            uint32_t s=packet+0x4408+104*i;
            X_MF32(s+12)=0;X_MF32(s+16)=0;X_MF32(s+20)=1;
            X_MF32(s+24)=0;X_MF32(s+28)=0;
            X_M16(s+32)=2;X_M8(s+34)=id%2;X_M32(s+36)=4;
            const float points[8]={-10,-10,10,-10,10,10,-10,10};
            memcpy(X_G(s+40),points,sizeof points);
        }
        unsigned bytes=65536+1024;
        uint8_t *initial=malloc(bytes),*reference=malloc(bytes);
        assert(initial&&reference);
        memcpy(initial,X_G(sp-1024),bytes);
        xctx input=*c;
        f_00170C10(c);
        xctx reference_ctx=*c;
        memcpy(reference,X_G(sp-1024),bytes);
        memcpy(X_G(sp-1024),initial,bytes);*c=input;
#endif
        unsigned before=shared_guarded_value;
        {
            XV_OBJECT_SOLVER_SCOPE(c);
            assert(!!xv_object_solver_token_==expected);
            assert(!xv_object_solver_begin(c));
            if(expected) {
                __atomic_add_fetch(&solver_entered,1,__ATOMIC_RELAXED);
                unsigned n=__atomic_add_fetch(&solver_running,1,__ATOMIC_SEQ_CST);
                unsigned old=__atomic_load_n(&solver_peak,__ATOMIC_SEQ_CST);
                while(n>old&&!__atomic_compare_exchange_n(&solver_peak,&old,n,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)) {}
                /* Make actual concurrent private work observable to the test.
                 * Other callbacks can request owner services while it runs. */
                struct timespec pause={0,2000000};nanosleep(&pause,NULL);
#ifndef OBJECT_SOLVER_TRANSLATED
                X_M32(inputs+32)=0x12345678;
#endif
                __atomic_sub_fetch(&solver_running,1,__ATOMIC_SEQ_CST);
            } else assert(shared_guarded_value==before);
#ifdef OBJECT_SOLVER_TRANSLATED
            f_00170C10(c);
            assert(!memcmp(c,&reference_ctx,sizeof *c));
            assert(!memcmp(X_G(sp-1024),reference,bytes));
            /* The complete captured packet and input vectors are read-only. */
            assert(!memcmp(X_G(packet),initial+1024+(packet-sp),0xac08));
            assert(!memcmp(X_G(inputs),initial+1024+(inputs-sp),28));
#endif
        }
#ifdef OBJECT_SOLVER_TRANSLATED
        /* Also execute the actual emitted cleanup hook through the solver's
         * returns, not just the equivalent surrounding fixture scope. */
        memcpy(X_G(sp-1024),initial,bytes);*c=input;
        f_solver_hooked(c);
        assert(!memcmp(c,&reference_ctx,sizeof *c));
        assert(!memcmp(X_G(sp-1024),reference,bytes));
#endif
        /* Cleanup must reacquire before any enclosing shared mutation. */
        before=shared_guarded_value;
        struct timespec pause={0,50000};nanosleep(&pause,NULL);
        assert(shared_guarded_value==before);
#ifdef OBJECT_SOLVER_TRANSLATED
        free(reference);free(initial);
#endif
    }
    *c=saved;
}
