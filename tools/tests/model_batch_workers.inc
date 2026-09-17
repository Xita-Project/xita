/* Focused extension of the real production worker-pool fixture. The owned
 * generated hierarchy/palette regions are supplied separately, never checked in.
 * Hierarchy runs on real workers; owner consumes published matrices after join. */
#include <xmmintrin.h>
void current_hierarchy(xctx *),candidate_hierarchy(xctx *);
void current_palette(xctx *),candidate_palette(xctx *);
void xv_model_palette_override(int);
void xv_model_palette_report(unsigned);
static float composition_matrices[300][8][13];
static unsigned composition_completed[300],composition_consumed;

static void hierarchy_job(xctx *c,unsigned id)
{
    xctx saved=*c;
    unsigned sp=c->r[4]-8192u,model=sp+512u,poses=sp+768u;
    unsigned nodes=sp+1024u,matrices=sp+2560u;
    memset(X_G(sp-64),0,4160);
    X_M32(model+0xb8)=8;X_M32(model+0xbc)=nodes;
    X_M32(sp+0x24)=matrices;X_M32(sp+0x28)=poses;X_M32(sp+0x2c)=model;
    X_M32(sp+0x10)=2;X_M16(sp+0x178)=0;X_M16(sp+0x17a)=1;
    for(unsigned n=0;n<8;n++) {
        X_M16(nodes+n*156+0x20)=n==7?0xffff:n+1;
        X_M16(nodes+n*156+0x22)=0xffff;
        X_M16(nodes+n*156+0x24)=n?n-1:0xffff;
        X_MF32(poses+n*32+12)=1;X_MF32(poses+n*32+28)=1;
        X_MF32(poses+n*32+16)=(float)id*.03125f;
    }
    X_MF32(matrices)=X_MF32(matrices+4)=X_MF32(matrices+20)=X_MF32(matrices+36)=1;
    c->r[4]=sp;c->r[0]=1;c->preempt=100;
    c->f_kind=XK_SUB;c->f_bits=32;c->f_cf_override=c->f_of_override=c->f_cf=c->f_of=1;
    c->fsp=id%8;c->fcw=0x37f;
    unsigned fp=_mm_getcsr();xctx initial=*c;
    uint8_t before[4160],after[4160];memcpy(before,X_G(sp-64),sizeof before);
    /* Odd jobs retain a larger existing transaction around both versions. */
    int outer=id&1?xv_object_math_lock():0;
    current_hierarchy(c);xctx expected=*c;
    unsigned expected_fp=_mm_getcsr();memcpy(after,X_G(sp-64),sizeof after);
    memcpy(X_G(sp-64),before,sizeof before);*c=initial;_mm_setcsr(fp);
    candidate_hierarchy(c);
    assert(!memcmp(c,&expected,sizeof *c));
    assert(!memcmp(X_G(sp-64),after,sizeof after));assert(_mm_getcsr()==expected_fp);
    /* This includes the last original child, unlike the old helper-only test. */
    for(unsigned n=0;n<8;n++) {
        float expected_matrix[13]={1,1,0,0,0,1,0,0,0,1,(float)(n*id)*.03125f,0,0};
        assert(!memcmp(X_G(matrices+n*52),expected_matrix,sizeof expected_matrix));
    }
    memcpy(composition_matrices[id],X_G(matrices),sizeof composition_matrices[id]);
    composition_completed[id]++;
    xv_object_math_unlock(&outer);*c=saved;_mm_setcsr(fp);
}

static void composition_owner_palette(unsigned round)
{
    assert(!active);
    /* Own distinct output storage; worker results become visible through join. */
    unsigned sp=0x82000,model=0x83000,nodes=0x84000,poses=0x85000;
    for(unsigned id=0;id<300;id++) {
        assert(composition_completed[id]==round+1);
        memset(X_G(sp-64),0,0x4100);
        X_M32(model+0xb8)=8;X_M32(model+0xbc)=nodes;
        memcpy(X_G(poses),composition_matrices[id],sizeof composition_matrices[id]);
        for(unsigned n=0;n<8;n++) {
            float bind[13]={1,1,0,0,0,1,0,0,0,1,.25f,0,0};
            memcpy(X_G(nodes+n*156+0x68),bind,sizeof bind);
        }
        xctx c={0};c.r[4]=sp;c.r[5]=model;c.r[7]=poses;c.preempt=100;c.fcw=0x37f;c.fsp=id%8;
        c.f_kind=XK_SUB;c.f_bits=32;c.f_cf_override=c.f_of_override=c.f_cf=c.f_of=1;
        unsigned fp=_mm_getcsr();xctx initial=c;uint8_t before[0x4100],after[0x4100];
        memcpy(before,X_G(sp-64),sizeof before);current_palette(&c);xctx expected=c;
        unsigned expected_fp=_mm_getcsr();memcpy(after,X_G(sp-64),sizeof after);
        memcpy(X_G(sp-64),before,sizeof before);c=initial;_mm_setcsr(fp);candidate_palette(&c);
        assert(!memcmp(&c,&expected,sizeof c));assert(!memcmp(X_G(sp-64),after,sizeof after));
        assert(_mm_getcsr()==expected_fp);composition_consumed++;_mm_setcsr(fp);
    }
}
