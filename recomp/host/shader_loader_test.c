/* Exercise the actual shader loader with fake Vita I/O; no GPU is needed. */
#include <assert.h>
#include <stdarg.h>
#include "../../runtime/xv_shader.c"
static int packaged = 1, device = 1, selected, invalid_program;
static unsigned parameter_mask, parameter_queries;
void xv_logf(const char *fmt, ...) { (void)fmt; }
SceUID sceIoOpen(const char *name, int flags, SceMode mode) {
    (void)flags; (void)mode;
    if (!strncmp(name, "app0:", 5)) return packaged ? 1 : -1;
    assert(!strncmp(name, "ux0:data/xita/shaders/", 22));
    return device ? 2 : -1;
}
int sceIoClose(SceUID fd) { (void)fd; return 0; }
SceOff sceIoLseek(SceUID fd, SceOff off, int whence) { (void)fd; (void)off; return whence == SCE_SEEK_END ? 64 : 0; }
SceSSize sceIoRead(SceUID fd, void *buf, SceSize n) { selected = fd; memset(buf, 0, n); return n; }
int sceGxmProgramCheck(const SceGxmProgram *p) { (void)p; return invalid_program ? -1 : SCE_OK; }
const SceGxmProgramParameter *sceGxmProgramFindParameterByName(const SceGxmProgram *p,const char *name)
{
    assert(p && (uintptr_t)p % 4 == 0);
    assert(strlen(name)==4 && !memcmp(name,"tex",3) && name[3]>='0' && name[3]<='3');
    parameter_queries++;
    return parameter_mask & (1u << (name[3]-'0')) ? (const SceGxmProgramParameter *)p : NULL;
}
int main(int argc, char **argv) {
    (void)argv;
    int override = argc > 1;
    if (override) setenv("XV_SHADER_OVERRIDE", "1", 1); else unsetenv("XV_SHADER_OVERRIDE");
    free(load_gxp("app0:shaders/test.gxp")); assert(selected == (override ? 2 : 1));
    packaged = 0; free(load_gxp("app0:shaders/test.gxp")); assert(selected == 2);
    packaged = 1; device = 0; free(load_gxp("app0:shaders/test.gxp")); assert(selected == 1);
    packaged = 0; assert(load_gxp("app0:shaders/test.gxp") == NULL);
    for (unsigned i = 0; i < sizeof xv_hud_gxp / sizeof xv_hud_gxp[0]; i++) {
        selected = 0; device = 0;
        void *p = load_gxp(xv_hud_gxp[i].path); assert(p && selected == 0);
        assert(!memcmp(p, xv_hud_gxp[i].data, xv_hud_gxp[i].size)); free(p);
        selected = 0; packaged = device = 1;
        p = load_gxp(xv_hud_gxp[i].path); assert(p);
        if (override) assert(selected == 2);
        else { assert(selected == 0); assert(!memcmp(p, xv_hud_gxp[i].data, xv_hud_gxp[i].size)); }
        free(p);
    }
    for (unsigned i = 0; i < sizeof xv_vs_embedded / sizeof xv_vs_embedded[0]; i++) {
        selected = 0; device = 0;
        void *p = load_gxp(xv_vs_embedded[i].path); assert(p && selected == 0);
        assert(!memcmp(p, xv_vs_embedded[i].data, xv_vs_embedded[i].size)); free(p);
        selected = 0; packaged = device = 1;
        p = load_gxp(xv_vs_embedded[i].path); assert(p);
        if (override) assert(selected == 2);
        else { assert(selected == 0); assert(!memcmp(p, xv_vs_embedded[i].data, xv_vs_embedded[i].size)); }
        free(p);
    }
    for (unsigned i = 0; i < sizeof xv_ps_embedded / sizeof xv_ps_embedded[0]; i++) {
        selected = 0; packaged = device = 0;
        void *p = load_gxp(xv_ps_embedded[i].path); assert(p && selected == 0);
        assert(!memcmp(p, xv_ps_embedded[i].data, xv_ps_embedded[i].size)); free(p);
        selected = 0; packaged = device = 1;
        p = load_gxp(xv_ps_embedded[i].path); assert(p);
        if (override) assert(selected == 2);
        else { assert(selected == 0); assert(!memcmp(p, xv_ps_embedded[i].data, xv_ps_embedded[i].size)); }
        free(p);
    }
    unsigned found=0;
    for (unsigned i=0;i<sizeof xv_ps_embedded / sizeof xv_ps_embedded[0];i++)
        if (!strcmp(xv_ps_embedded[i].path,"app0:shaders/ps_28CF808C_07_na.frag.gxp")) {
            selected=0;packaged=device=1;
            void *p=load_gxp("builtin:xita-depth");assert(p&&selected==0);
            assert(!memcmp(p,xv_ps_embedded[i].data,xv_ps_embedded[i].size));free(p);found++;
        }
    assert(found==1);
    assert(xv_fshader_embedded_texture_mask(NULL)==15);
    assert(xv_fshader_embedded_texture_mask("app0:shaders/unknown.frag.gxp")==15);
    unsigned tested=0;
    for(unsigned i=0;i<sizeof xv_ps_embedded/sizeof xv_ps_embedded[0];i++) {
        for(parameter_mask=0;parameter_mask<16;parameter_mask++) {
            selected=0;parameter_queries=0;
            assert(xv_fshader_embedded_texture_mask(xv_ps_embedded[i].path)==(override?15:parameter_mask));
            assert(!selected && parameter_queries==(override?0u:4u));tested++;
        }
    }
    invalid_program=1;parameter_queries=0;
    assert(xv_fshader_embedded_texture_mask(xv_ps_embedded[0].path)==15 && !parameter_queries);
    printf("PASS: shader precedence, embedded fallbacks, protected depth and %u metadata masks without I/O; override/unknown/invalid programs retain every stage\n",tested);
}
