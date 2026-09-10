#include <assert.h>
#include "../kernel/xd3d.c"
uint8_t *g_xram;
xk_thread *xk_cur;
static unsigned definition_logs, capture_limit_logs;
void xk_os_log(const char *fmt, ...)
{
    if (strstr(fmt, "[psdef]")) {
        va_list ap; va_start(ap, fmt);
        unsigned hash = va_arg(ap, unsigned);
        const char *hex = va_arg(ap, const char *);
        assert(hash == xd3d_state.ps_hash && strlen(hex) == 480);
        const uint8_t *bytes = (const uint8_t *)xd3d_state.ps_shadow;
        for (unsigned i = 0; i < 240; ++i) {
            char expected[3]; snprintf(expected, sizeof expected, "%02X", bytes[i]);
            assert(hex[2*i] == expected[0] && hex[2*i+1] == expected[1]);
        }
        va_end(ap); ++definition_logs;
    }
    if (strstr(fmt, "shader definition capture limit")) ++capture_limit_logs;
}
static uint32_t rng=13;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void check(void)
{
    xd3d_ps_sync();
    assert(xd3d_state.ps_hash==psdef_hash((uint8_t *)xd3d_state.ps_shadow));
    assert(xd3d_state.ps_key==xv_ps_program_key(xd3d_state.ps_shadow));
    for(unsigned i=0;i<18;i++) {
        unsigned off=i<8?0x28+i*4:i<16?0x48+(i-8)*4:0xAC+(i-16)*4;
        uint32_t color=xd3d_state.ps_shadow[off/4];
        float expected[]={((color>>16)&255)/255.0f,((color>>8)&255)/255.0f,(color&255)/255.0f,(color>>24)/255.0f};
        assert(!memcmp(expected,xd3d_state.psc[i],sizeof expected));
    }
}
int main(int argc,char **argv)
{
    (void)argv;setenv("XV_PREP_STATE_CACHE",argc>1?"0":"1",1);
    xk_mem_setup(0x10000,0x400000);g_xram=calloc(1,xk_mem_arena_size());assert(g_xram);xk_mem_bind_arena();
    uint32_t stack=xk_kalloc(64),def=xk_kalloc(16384);xctx c={0};
    /* The first all-zero program still needs its identity and colors resolved. */
    c.r[4]=stack;X_M32(stack+4)=def;
    xv_hle_D3DDevice_SetPixelShaderProgram(&c);assert(xd3d_state.ps_dirty);check();
    memset(&xd3d_state,0,sizeof xd3d_state);ps_synced=0;
    c.r[4]=stack;c.r[1]=0x260;c.r[2]=0;
    xv_hle_D3DDevice_SetRenderState_Simple(&c);assert(xd3d_state.ps_dirty);check();
    uint32_t definitions[80][60];
    for(unsigned i=0;i<80;i++)for(unsigned j=0;j<60;j++)definitions[i][j]=next();
    for(unsigned i=0;i<6000;i++) {
        unsigned choice=i%80;
        definitions[choice][0xD4/4]=(definitions[choice][0xD4/4]&~255u)|(i%9);
        x_guest_write(def,definitions[choice],240);
        c.r[4]=stack;X_M32(stack+4)=def;xv_hle_D3DDevice_SetPixelShaderProgram(&c);check();
        uint32_t h=xd3d_state.ps_hash,k=xd3d_state.ps_key;
        c.r[1]=0xA60+(i%16)*4;c.r[2]=next();c.r[4]=stack;xv_hle_D3DDevice_SetRenderState_Simple(&c);check();
        assert(xd3d_state.ps_hash==h&&xd3d_state.ps_key==k);
        assert(!xd3d_state.ps_dirty);c.r[4]=stack;xv_hle_D3DDevice_SetRenderState_Simple(&c);assert(!xd3d_state.ps_dirty);
        c.r[1]=0x1E60;c.r[2]=i%9;c.r[4]=stack;xv_hle_D3DDevice_SetRenderState_Simple(&c);check();
    }
    if(argc==1)assert(ps_identity_cache.adjacent_hits>5000&&ps_identity_cache.misses>0);
    /* Repeated upload, reset, and mutation at the same address remain correct. */
    x_guest_write(def,xd3d_state.ps_shadow,240);c.r[4]=stack;X_M32(stack+4)=def;
    xv_hle_D3DDevice_SetPixelShaderProgram(&c);assert(!xd3d_state.ps_dirty);
    c.r[4]=stack;X_M32(stack+4)=0;xv_hle_D3DDevice_SetPixelShaderProgram(&c);check();
    /* Definition crosses separately mapped pages. */
    uint32_t boundary=(def+8191)&~4095u;g_xpt[(boundary>>12)+1]=g_xpt[(boundary>>12)+2];
    uint32_t split=boundary+4096-100;x_guest_write(split,definitions[3],240);
    c.r[4]=stack;X_M32(stack+4)=split;xv_hle_D3DDevice_SetPixelShaderProgram(&c);
    assert(!memcmp(xd3d_state.ps_shadow,definitions[3],240));check();
    /* A hash match without matching program bytes cannot return a cached key. */
    xv_ps_identity_cache cache={0};uint32_t hash,key;
    xv_ps_identity_lookup(&cache,definitions[0],&hash,&key);
    cache.entries[cache.last].key^=1;cache.entries[cache.last].program[0]^=1;
    xv_ps_identity_lookup(&cache,definitions[0],&hash,&key);
    assert(key==xv_ps_program_key(definitions[0])&&cache.misses==2);
    xv_ps_identity_lookup(&cache,definitions[1],&hash,&key);
    xv_ps_identity_lookup(&cache,definitions[0],&hash,&key);
    assert(hash==psdef_hash((uint8_t *)definitions[0])&&key==xv_ps_program_key(definitions[0]));
    /* More than 512 distinct programs must not restart logging on every draw.
       All identity/color assertions above still apply after capture saturates. */
    assert(definition_logs == 512 && capture_limit_logs == 1);
    free(g_xram);free(g_xpt);
    puts("PASS: production shader identity/color sync, 18000 updates, reuse/collisions, repeated state, mutable upload, reset and split pages");
}
