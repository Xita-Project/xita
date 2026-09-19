#define _DEFAULT_SOURCE
#define XV_NATIVE_MODEL_PALETTE 1
#define XV_EXPERIMENTAL_OBJECT_JOBS 1
#define XV_OWNER_PHASE 1
#define XV_POSE_PIPELINE 1
#include "../../recomp/kernel/xk_palette.c"
#include "../../recomp/kernel/xk_pose_pipeline.c"
#include <assert.h>
#include <unistd.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
int xv_object_math_lock(void){return 0;}
void xv_object_math_unlock(int *p){(void)p;}
int xv_object_jobs_native_idle(void){return 1;}
int xv_owner_phase_active(void *c,unsigned p,uint32_t *g){(void)c;(void)p;*g=1;return 1;}
void xk_os_log(const char *f,...){(void)f;}
void xv_logf(const char *f,...){(void)f;}
static void complete(void){for(unsigned i=0;LOAD(pending)&&i<100000;i++)usleep(10);assert(!LOAD(pending));}
enum { MODEL=0x1000,POSE=0x2000,BIND=0x4000,SP=0x9000,ENTRY=0x8000 };
static xctx setup(unsigned count,unsigned variant)
{
    xctx c={0};c.r[4]=SP;c.r[5]=MODEL;c.r[7]=POSE;c.preempt=100;c.fcw=0x37f;
    X_M32(MODEL+0xb8)=count;X_M32(MODEL+0xbc)=BIND;
    X_M32(ENTRY)=0xabcd0001;
    for(unsigned n=0;n<count;n++) {
        float *l=X_G(POSE+n*52),*r=X_G(BIND+0x68+n*156);
        memset(l,0,52);memset(r,0,52);
        l[0]=l[1]=l[5]=l[9]=1;r[0]=r[1]=r[5]=r[9]=1;
        l[10]=(float)(variant*10+n);l[11]=2;l[12]=3;
        r[10]=4;r[11]=5;r[12]=6;
    }
    return c;
}
static unsigned enter(xctx *c){unsigned pose=c->r[7];c->r[7]=ENTRY;unsigned token=xv_pose_scope_begin(c);c->r[7]=pose;assert(token);return token;}
int main(void)
{
    g_xram=calloc(1,1<<20);g_img_base=g_xram;g_xpt=calloc(256,4);
    assert(g_xram&&g_xpt);for(unsigned i=0;i<256;i++)g_xpt[i]=i*4096;
    setenv("XV_NATIVE_MODEL_PALETTE","1",1);
    unsigned counts[]={1,4,16,64};
    for(unsigned k=0;k<4;k++) {
        unsigned n=counts[k];float previous[64][13];
        xv_pose_pipeline_invalidate();
        xv_pose_pipeline_begin(1);xctx c=setup(n,1);unsigned token=enter(&c);
        assert(xv_math_model_palette(&c));xv_pose_scope_end(&token);
        memcpy(previous,X_G(SP+0xe4),n*52);xv_pose_pipeline_end();complete();
        xctx baseline=setup(n,2);assert(xv_math_model_palette(&baseline));
        float current[64][13];memcpy(current,X_G(SP+0xe4),n*52);
        assert(memcmp(current,previous,n*52));
        xv_pose_pipeline_begin(2);c=setup(n,2);token=enter(&c);
        assert(xv_math_model_palette(&c));xv_pose_scope_end(&token);
        assert(!memcmp(previous,X_G(SP+0xe4),n*52));
        assert(!memcmp(&baseline,&c,sizeof c)); /* current continuation retained */
        xv_pose_pipeline_end();complete();
        assert(hits>=1);
    }
    xv_pose_pipeline_shutdown();free(g_xpt);free(g_xram);
    puts("PASS production palette integration: 1/4/16/64 complete prior poses, current register continuation, no mixed final bone");
}
