#include "menu_combiner.c"
#include <assert.h>
#include <stdio.h>
#include <math.h>

static int feq(float a, float b){ float d=a-b; return d<2e-3f && d>-2e-3f; }
static const float FOG[4]={0,0,0,1};

static void test_passthrough_tex0(void)
{
    /* stage0: AB = t0 * (invert(zero)=1) -> r0 ; final D = r0 -> out = t0 */
    menu_combiner cb; memset(&cb,0,sizeof cb);
    cb.stages = 1;
    cb.rgb_in[0]  = (0x08u<<24) | (0x20u<<16) | 0; /* A=t0(id), B=zero(invert=1), C=D=zero */
    cb.rgb_out[0] = (0xCu<<4);                      /* ab -> r0(0xC), identity */
    cb.final_abcd = 0x0000000C;                     /* D = r0 */
    float tex[4][4]={{0.5f,0.6f,0.7f,1.0f},{0,0,0,1},{0,0,0,1},{0,0,0,1}};
    float diff[4]={1,1,1,1}, spec[4]={0,0,0,0}, out[4];
    menu_combiner_eval(&cb, tex, diff, spec, FOG, out);
    assert(feq(out[0],0.5f)&&feq(out[1],0.6f)&&feq(out[2],0.7f));
}

static void test_diffuse_times_tex(void)
{
    /* stage0: AB = t0 * v0 -> r0 ; final D=r0 -> out = t0*diffuse */
    menu_combiner cb; memset(&cb,0,sizeof cb);
    cb.stages = 1;
    cb.rgb_in[0]  = (0x08u<<24) | (0x04u<<16) | 0; /* A=t0, B=v0(diffuse) */
    cb.rgb_out[0] = (0xCu<<4);
    cb.final_abcd = 0x0000000C;
    float tex[4][4]={{1.0f,1.0f,1.0f,1.0f},{0,0,0,1},{0,0,0,1},{0,0,0,1}};
    float diff[4]={0.25f,0.5f,1.0f,1.0f}, spec[4]={0,0,0,0}, out[4];
    menu_combiner_eval(&cb, tex, diff, spec, FOG, out);
    assert(feq(out[0],0.25f)&&feq(out[1],0.5f)&&feq(out[2],1.0f));
}

static void test_dot_product(void)
{
    /* stage0: AB = dot(t0.expand, t1.expand) -> r0 ; final D=r0.
     * t0=t1=(1,0.5,0.5) expand -> (1,0,0); dot=1. Broadcast. */
    menu_combiner cb; memset(&cb,0,sizeof cb);
    cb.stages = 1;
    cb.rgb_in[0]  = ((0x40u|0x08u)<<24) | ((0x40u|0x09u)<<16); /* A=t0 expand_normal, B=t1 expand_normal */
    cb.rgb_out[0] = (0xCu<<4) | (0x02u<<12);        /* ab->r0, AB_DOT flag */
    cb.final_abcd = 0x0000000C;
    float tex[4][4]={{1.0f,0.5f,0.5f,1.0f},{1.0f,0.5f,0.5f,1.0f},{0,0,0,1},{0,0,0,1}};
    float diff[4]={1,1,1,1}, spec[4]={0,0,0,0}, out[4];
    menu_combiner_eval(&cb, tex, diff, spec, FOG, out);
    assert(feq(out[0],1.0f)&&feq(out[1],1.0f)&&feq(out[2],1.0f)); /* dot broadcast */
}

static void test_final_lerp(void)
{
    /* No stages. final rgb = A*B + (1-A)*C + D with A=zero(->0), C=v0, D=zero:
     * out = (1-0)*v0 = v0. */
    menu_combiner cb; memset(&cb,0,sizeof cb);
    cb.stages = 0;
    cb.final_abcd = (0x00u<<24)|(0x00u<<16)|(0x04u<<8)|0x00; /* A=zero B=zero C=v0 D=zero */
    cb.final_efg = 0x00000004;                                /* G=v0 -> alpha */
    float tex[4][4]={{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1}};
    float diff[4]={0.2f,0.4f,0.8f,0.9f}, spec[4]={0,0,0,0}, out[4];
    menu_combiner_eval(&cb, tex, diff, spec, FOG, out);
    assert(feq(out[0],0.2f)&&feq(out[1],0.4f)&&feq(out[2],0.8f));
}


static void test_final_combiner_uses_final_factors(void)
{
    /* stage0: r0 = dot(t0, c0=luma weights); final: A=r0, B=c0 -> tint must be the FINAL c0
     * (SET_SPECULAR_FOG_FACTOR0), not the stage-0 weights. */
    menu_combiner cb; memset(&cb,0,sizeof cb);
    cb.stages = 1;
    cb.rgb_in[0]  = (0x08u<<24) | (0x01u<<16);     /* A=t0 B=c0 */
    cb.rgb_out[0] = (0xCu<<4) | (0x2u<<12);        /* ab -> r0, AB dot */
    cb.factor0[0] = 0x0080B333;                     /* luma weights .5,.7,.2 */
    cb.final_factor0 = 0x00FF8040;                  /* tint 1.0,.5,.25 */
    cb.final_abcd = 0x0C010000;                     /* rgb = r0*c0 */
    cb.final_efg  = 0x00000080;
    float tex[4][4]={{1,1,1,1},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
    float diff[4]={1,1,1,1}, spec[4]={0,0,0,0}, out[4];
    menu_combiner_eval(&cb, tex, diff, spec, FOG, out);
    /* luma of white = 1.4 -> saturates to 1; out = tint */
    assert(feq(out[0],1.0f) && feq(out[1],0.5f) && feq(out[2],0.25f));
}


static void test_decode_scans_every_stage(void)
{
    /* decode() must know the stage count before prepare() scans the stages: two stages,
     * stage 0 reading t3 and stage 1 reading t2 (rgb) / t1 (alpha), final combiner on r0
     * only -> tex_used covers units 0 (always), 1, 2 and 3, and stage 1's constant is
     * unpacked. With the count set after the scan, only unit 0 was ever bound. */
    h2_command_state s; memset(&s, 0, sizeof s);
    s.setup[0x1E60 / 4] = 2;                                  /* SET_COMBINER_CONTROL: 2 stages */
    s.setup[(0xAC0 + 0) / 4] = (0x0Bu << 24) | (0x20u << 16); /* stage 0 rgb: A=t3, B=1 */
    s.setup[(0xAC0 + 4) / 4] = (0x0Au << 24) | (0x0Cu << 16); /* stage 1 rgb: A=t2, B=r0 */
    s.setup[(0x260 + 4) / 4] = (0x09u << 24) | (0x20u << 16); /* stage 1 alpha: A=t1 */
    s.setup[(0xA60 + 4) / 4] = 0x80402010u;                    /* stage 1 factor0 */
    s.setup[0x288 / 4] = 0x0000000C;                          /* final D = r0 */
    menu_combiner cb;
    menu_combiner_decode(&s, &cb);
    assert(cb.stages == 2 && cb.prepared);
    assert(cb.tex_used == 0xF);
    assert(feq(cb.c0f[1][0], 0x40 / 255.0f) && feq(cb.c0f[1][1], 0x20 / 255.0f) && feq(cb.c0f[1][2], 0x10 / 255.0f) && feq(cb.c0f[1][3], 0x80 / 255.0f));
    /* One stage that reads nothing but v0: only unit 0 (r0.a seed). */
    s.setup[0x1E60 / 4] = 1; s.setup[(0xAC0 + 0) / 4] = (0x04u << 24) | (0x20u << 16);
    menu_combiner_decode(&s, &cb);
    assert(cb.stages == 1 && cb.tex_used == 0x1);
}
int main(void)
{
    test_final_combiner_uses_final_factors();
    test_passthrough_tex0();
    test_diffuse_times_tex();
    test_dot_product();
    test_final_lerp();
    test_decode_scans_every_stage();
    printf("menu_combiner_test: all assertions passed\n");
    return 0;
}
