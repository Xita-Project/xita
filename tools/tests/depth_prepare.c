#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include "runtime/xv_depth_prepare.h"
#include "runtime/xv_stencil.h"
#define XV_MAX_VS 96
#define XV_MAX_STREAMS 16
#define XV_PS_TABLE_COUNT 600
enum { FS_COLOR, FS_TEXMOD, FS_TEX0, FS_LM, FS_KINDS };
enum { BLEND_MODES=24, BLEND_NOCOLOR=23 };
typedef struct { uint32_t words[4]; } SceGxmTexture;
typedef void SceGxmFragmentProgram;
typedef struct { int unused; } xv_vs_desc_t;
typedef struct {
    SceGxmFragmentProgram *fprog;
    unsigned alpha_test_mode, uses_discard, replaces_depth;
    int tex_index[4];
} xv_fshader_t;
typedef struct { xv_fshader_t fs[FS_KINDS][BLEND_MODES]; } vs_slot_t;
static vs_slot_t g_vs[XV_MAX_VS];
static struct { unsigned mask; } g_blend_combo[BLEND_MODES];
static struct { uint32_t ps_key; unsigned cube_modes, cube_mask; } xv_ps_table[XV_PS_TABLE_COUNT];
static int cutout_enabled(void) { return 0; }
static unsigned texture_calls, link_calls, depth_links, depth_draws;
static xv_fshader_t linked_program;
static int fail_original, fail_depth;
static xv_fshader_t *fragment_for_ps_mode(vs_slot_t *v,int entry,unsigned blend,int mode)
{
    (void)v;(void)entry;(void)blend;(void)mode;link_calls++;
    return fail_original ? NULL : &linked_program;
}
static SceGxmFragmentProgram *fragment_for(vs_slot_t *v,unsigned kind,unsigned blend,void *unused)
{
    (void)unused;
    if (kind==FS_COLOR && blend==BLEND_NOCOLOR) {
        depth_links++;
        if (fail_depth) return NULL;
    }
    return v->fs[kind][blend].fprog;
}
static void xv_render_profile_depth_only(unsigned count) { assert(count==6);depth_draws++; }
#define XV_LOG(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define XV_RENDER_CALL(stage, expression) (expression)
#define record_textures(c,d,immediate) (++texture_calls, (void)(c), (void)(d), (void)(immediate), 7u)
#include "depth_prepare.inc"

static xv_fshader_t *replay(cmd_t *c, unsigned *cube)
{
    for (unsigned i=0;i<1;i++) {
        unsigned frame=123;
        #include "depth_replay.inc"
        *cube=cube_mask;
        return fs;
    }
    return NULL;
}

static cmd_t query(void)
{
    cmd_t c={0};c.vs=56;c.ps_entry=141;c.blend=5;c.atest=0x47f;
    c.visibility=183;c.index_count=6;c.depth_func_idx=3;c.cull=3;
    c.streams[0]=(void*)(uintptr_t)0x10000;c.indices=(void*)(uintptr_t)0x20000;
    return c;
}

static uint32_t release_writer, writer_done, published_payload;
static xv_depth_proofs threaded;
static void *publish_thread(void *unused)
{
    (void)unused;
    while (!__atomic_load_n(&release_writer,__ATOMIC_ACQUIRE)) sched_yield();
    published_payload=0x31415926; /* read only after acquiring the proof */
    xv_depth_proof_publish(&threaded, xv_depth_proof_key(56,141,5));
    __atomic_store_n(&writer_done,1,__ATOMIC_RELEASE);
    return NULL;
}

int main(int argc, char **argv)
{
    (void)argv;
    if(argc>1) {
        assert(!xv_depth_prepare_available());
        xv_depth_prepare_override(1);assert(!depth_prepare_enabled());
        cmd_t c=query();xv_depth_proof_publish(&g_depth_proofs,depth_prepare_key(&c));
        assert(record_material(&c,NULL,1)==7 && !c.depth_prepared);
        xv_depth_prepare_override(-1);assert(!depth_prepare_enabled());
        puts("PASS: incompatible shader options refuse early preparation despite a cached proof");
        return 0;
    }
    int compatible=!getenv("XV_SHADER_OVERRIDE") || !atoi(getenv("XV_SHADER_OVERRIDE"));
    int configured=getenv("XV_DEPTH_PREPARE") && atoi(getenv("XV_DEPTH_PREPARE"));
    assert(depth_prepare_enabled()==(compatible && configured));
    xv_depth_prepare_override(1);assert(depth_prepare_enabled()==compatible);
    for (unsigned v=0;v<XV_MAX_VS;v++) for (unsigned b=0;b<BLEND_MODES;b++) {
        for (unsigned f=0;f<FS_KINDS;f++) {
            g_vs[v].fs[f][b].fprog=(void*)(uintptr_t)(1+v*128+f*24+b);
            for(unsigned t=0;t<4;t++) g_vs[v].fs[f][b].tex_index[t]=-1;
        }
    }
    linked_program=(xv_fshader_t){.fprog=(void*)1,.alpha_test_mode=1};
    cmd_t c=query(), before=c;
    assert(record_material(&c,NULL,1)==7 && !c.depth_prepared && texture_calls==1);
    assert(!memcmp(&c,&before,sizeof c));
    unsigned cube=999;
    xv_fshader_t *constant=&g_vs[c.vs].fs[FS_COLOR][BLEND_NOCOLOR];
    assert(replay(&c,&cube)==constant && !cube && link_calls==1 && depth_draws==1);
    assert(xv_depth_proof_read(&g_depth_proofs,depth_prepare_key(&c)));
    c=query();before=c;
    unsigned calls=texture_calls;
    assert(record_material(&c,NULL,1)==(compatible?0:7));
    assert(c.depth_prepared==compatible && texture_calls==calls+!compatible);
    before.depth_prepared=compatible;assert(!memcmp(&c,&before,sizeof c));
    if (compatible) {
        /* Publication fixed this command. Later controls, cache collisions and
         * an unrelated failed link cannot make replay sample missing textures. */
        xv_depth_prepare_override(0);calls=link_calls;unsigned d=depth_links;
        memset(&g_depth_proofs,0,sizeof g_depth_proofs);fail_original=fail_depth=1;
        assert(replay(&c,&cube)==constant && !cube && link_calls==calls && depth_links==d);
        assert(c.visibility==183 && c.indices==before.indices && c.streams[0]==before.streams[0]);
        fail_original=fail_depth=0;xv_depth_prepare_override(1);
    }
    /* No proof from failed originals, failed constant links, mode-0 fallback,
     * discard/depth output, active alpha tests, a cube family or a color write. */
    for(unsigned bad=0;bad<12;bad++) {
        memset(&g_depth_proofs,0,sizeof g_depth_proofs);c=query();
        linked_program.alpha_test_mode=1;linked_program.uses_discard=linked_program.replaces_depth=0;
        fail_original=fail_depth=0;g_blend_combo[5].mask=0;xv_ps_table[141].cube_modes=0;
        switch(bad) {
        case 0:fail_original=1;break;case 1:fail_depth=1;break;
        case 2:linked_program.alpha_test_mode=0;break;case 3:linked_program.uses_discard=1;break;
        case 4:linked_program.replaces_depth=1;break;case 5:c.atest|=1u<<16;break;
        case 6:xv_ps_table[141].cube_modes=1;break;case 7:g_blend_combo[5].mask=1;break;
        case 8:c.ps_entry=-1;break;case 9:c.ps_entry=XV_PS_TABLE_COUNT;break;
        case 10:c.vs=XV_MAX_VS;break;case 11:c.blend=BLEND_MODES;break;
        }
        if(bad<8) (void)replay(&c,&cube);
        else depth_prepare_learn(&c,&linked_program,constant);
        assert(!xv_depth_proof_read(&g_depth_proofs,xv_depth_proof_key(56,141,5)));
        assert(record_material(&c,NULL,1)==7 && !c.depth_prepared);
    }
    fail_original=fail_depth=0;g_blend_combo[5].mask=0;xv_ps_table[141].cube_modes=0;
    linked_program.alpha_test_mode=1;linked_program.uses_discard=linked_program.replaces_depth=0;
    c=query();c.atest=0x107ff; /* enabled ALWAYS also preserves coverage */
    assert(replay(&c,&cube)==constant);
    for(unsigned changed=0;changed<3;changed++) {
        cmd_t other=c;
        if(changed==0)other.vs++;else if(changed==1)other.ps_entry++;else other.blend++;
        assert(record_material(&other,NULL,1)==7 && !other.depth_prepared);
    }
    uint32_t key=xv_depth_proof_key(56,141,5), collision=key+1;
    while(xv_depth_proof_slot(collision)!=xv_depth_proof_slot(key))collision++;
    xv_depth_proof_publish(&g_depth_proofs,collision);
    assert(!xv_depth_proof_read(&g_depth_proofs,key));
    assert(record_material(&c,NULL,1)==7 && !c.depth_prepared);
    assert(!xv_depth_proof_key(256,0,0) && !xv_depth_proof_key(0,-1,0));
    assert(!xv_depth_proof_key(0,65536,0) && !xv_depth_proof_key(0,0,32));
    xv_depth_proof_publish(&threaded,0);assert(!xv_depth_proof_read(&threaded,0));
    pthread_t thread;assert(!pthread_create(&thread,NULL,publish_thread,NULL));
    for(unsigned i=0;i<10000;i++)assert(!xv_depth_proof_read(&threaded,key));
    __atomic_store_n(&release_writer,1,__ATOMIC_RELEASE);
    while(!xv_depth_proof_read(&threaded,key))sched_yield();
    assert(published_payload==0x31415926);
    assert(!pthread_join(thread,NULL) && __atomic_load_n(&writer_done,__ATOMIC_ACQUIRE));
    xv_depth_prepare_override(-1);assert(depth_prepare_enabled()==(compatible && configured));
    puts("PASS: production depth recording/replay, delayed publication, collisions, exact variant guards, in-flight policy retention and restored config");
}
