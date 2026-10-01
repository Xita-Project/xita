/* Production sampler preparation with GPU calls replaced by descriptor writes.
 * Timings measure this host replay only, never Vita rendering performance. */
#define XV_RUN_RECOMP 1
#include <assert.h>
#include <time.h>
#include "../../runtime/xv_d3d.c"
static SceGxmTexture live;
static unsigned config, resolves, prepares;
static uint32_t header[8];
void xv_logf(const char *fmt,...) { (void)fmt; }
void *xv_guest_ptr(uint32_t address) { (void)address; return header; }
const SceGxmTexture *xv_ui_gxm_texture_pal(uint32_t h,uint32_t p)
{ (void)h; (void)p; resolves++; return &live; }
unsigned xv_ui_gxm_texture_options_key(void) { return config; }
void xv_ui_gxm_apply_texture_options(SceGxmTexture *t)
{
    prepares++;
    if(config&3) t->generic.min_filter=t->generic.mag_filter=(config&3)==1?0:1;
    if(!(config&4)) t->generic.mip_filter=0;
}
int sceGxmTextureSetMinFilter(SceGxmTexture *t,SceGxmTextureFilter v) { t->generic.min_filter=v; return 0; }
int sceGxmTextureSetMagFilter(SceGxmTexture *t,SceGxmTextureFilter v) { t->generic.mag_filter=v; return 0; }
int sceGxmTextureSetUAddrMode(SceGxmTexture *t,SceGxmTextureAddrMode v) { t->generic.uaddr_mode=v; return 0; }
int sceGxmTextureSetVAddrMode(SceGxmTexture *t,SceGxmTextureAddrMode v) { t->generic.vaddr_mode=v; return 0; }
static uint64_t ns(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;
}
int main(void)
{
    setenv("XV_SAMPLER_CACHE", "1", 1);
    setenv("XV_SAMPLER_CACHE_WAYS", "4", 1);
    config=4;
    /* Descriptor changes and live settings changes must match uncached output.
     * The source and earlier recorded copies must stay immutable. */
    for(unsigned n=0;n<10000;n++) {
        unsigned stage=n%4;
        memset(&live,0,sizeof live);
        ((uint32_t *)&live)[1]=0x10000+(n/7)%7;
        live.generic.mip_count=3; live.generic.mip_filter=1;
        config=(n/13)%3 | (((n/29)%2)<<2);
        S.tex_min[stage]=n%2?X_D3DTEXF_POINT:X_D3DTEXF_LINEAR;
        S.tex_mag[stage]=n%3?X_D3DTEXF_POINT:X_D3DTEXF_LINEAR;
        S.tex_addr_u[stage]=X_D3DTADDRESS_WRAP;
        S.tex_addr_v[stage]=X_D3DTADDRESS_CLAMP;
        SceGxmTexture original=live, expected=live;
        expected.generic.min_filter=S.tex_min[stage]==X_D3DTEXF_POINT?0:1;
        expected.generic.mag_filter=S.tex_mag[stage]==X_D3DTEXF_POINT?0:1;
        expected.generic.uaddr_mode=SCE_GXM_TEXTURE_ADDR_REPEAT;
        expected.generic.vaddr_mode=SCE_GXM_TEXTURE_ADDR_CLAMP;
        if(config&3)expected.generic.min_filter=expected.generic.mag_filter=(config&3)==1?0:1;
        if(!(config&4))expected.generic.mip_filter=0;
        SceGxmTexture recorded=*texture_for(stage);
        assert(!memcmp(&recorded,&expected,sizeof recorded));
        assert(!memcmp(&live,&original,sizeof live));
        config^=4;
        assert(texture_for(stage));
        assert(!memcmp(&recorded,&expected,sizeof recorded));
    }
    const unsigned count=1000000;
    for(unsigned materials=1;materials<=16;materials*=2) {
        config=4; memset(&live,0,sizeof live); resolves=prepares=0;
        uint64_t begin=ns(); uint32_t checksum=0;
        for(unsigned i=0;i<count;i++) {
            ((uint32_t *)&live)[1]=0x20000+materials*32+i%materials;
            const SceGxmTexture *t=texture_for(0);
            checksum+=((const uint32_t *)t)[1];
        }
        uint64_t elapsed=ns()-begin;
        assert(resolves==count);
        assert(prepares==(materials<=XV_SAMPLER_CACHE_WAYS?materials:count));
        printf("slots=%u materials=%u binds=%u preparations=%u host_ns_per_bind=%.2f checksum=%u\n",
               XV_SAMPLER_CACHE_WAYS,materials,count,prepares,(double)elapsed/count,checksum);
    }
    puts("PASS: sampler replay preserves descriptors, source mip metadata, captured draws and live options");
}
