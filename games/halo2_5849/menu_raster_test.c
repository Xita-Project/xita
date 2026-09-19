/* Rasterizer coverage, interpolation, blend, scissor and combiner shading. */
#include "menu_raster.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t buf[8 * 8 * 4];
static menu_raster_state st;

static void setup(int blend)
{
    memset(buf, 0, sizeof buf);
    memset(&st, 0, sizeof st);
    st.target.pixels = buf; st.target.width = 8; st.target.height = 8; st.target.pitch = 8 * 4;
    st.blend = blend; st.combiner = NULL;              /* diffuse passthrough */
    st.clip_x0 = 0; st.clip_y0 = 0; st.clip_x1 = 7; st.clip_y1 = 7;
}
static uint8_t B(int x, int y) { return buf[(y * 8 + x) * 4 + 0]; }
static uint8_t G(int x, int y) { return buf[(y * 8 + x) * 4 + 1]; }
static uint8_t R(int x, int y) { return buf[(y * 8 + x) * 4 + 2]; }
static menu_vertex_out V(float x, float y, float r, float g, float b, float a, float u, float v)
{
    menu_vertex_out o; memset(&o, 0, sizeof o);
    o.x = x; o.y = y; o.z = 0; o.w = 1;
    o.color[0] = r; o.color[1] = g; o.color[2] = b; o.color[3] = a;
    for (unsigned t = 0; t < 4; ++t) { o.uv[t][0] = u; o.uv[t][1] = v; }
    return o;
}

static void test_flat_coverage(void)
{
    setup(MENU_BLEND_OPAQUE);
    menu_vertex_out a = V(0, 0, 1, 0, 0, 1, 0, 0), b = V(8, 0, 1, 0, 0, 1, 0, 0), c = V(0, 8, 1, 0, 0, 1, 0, 0);
    menu_raster_triangle(&st, &a, &b, &c);
    assert(R(1, 1) == 255 && G(1, 1) == 0 && B(1, 1) == 0);
    assert(R(6, 6) == 0 && B(6, 6) == 0);
}
static void test_color_interpolation(void)
{
    setup(MENU_BLEND_OPAQUE);
    menu_vertex_out a = V(0, 0, 1, 0, 0, 1, 0, 0), b = V(8, 0, 0, 1, 0, 1, 0, 0), c = V(0, 8, 1, 0, 0, 1, 0, 0);
    menu_raster_triangle(&st, &a, &b, &c);
    assert(G(6, 0) > R(6, 0));
    assert(R(0, 1) > G(0, 1));
}
static void test_alpha_blend(void)
{
    setup(MENU_BLEND_ALPHA);
    for (int i = 0; i < 8 * 8; ++i) buf[i * 4 + 0] = 255;
    menu_vertex_out a = V(0, 0, 1, 0, 0, 0.5f, 0, 0), b = V(8, 0, 1, 0, 0, 0.5f, 0, 0), c = V(0, 8, 1, 0, 0, 0.5f, 0, 0);
    menu_raster_triangle(&st, &a, &b, &c);
    assert(R(1, 1) >= 125 && R(1, 1) <= 129);
    assert(B(1, 1) >= 125 && B(1, 1) <= 129);
}
static void test_combiner_texture(void)
{
    setup(MENU_BLEND_OPAQUE);
    /* combiner: r0 = t0 * v0 ; final D = r0 -> out = tex * diffuse */
    static menu_combiner cb;
    memset(&cb, 0, sizeof cb);
    cb.stages = 1;
    cb.rgb_in[0] = (0x08u << 24) | (0x04u << 16);   /* A=t0 B=v0 */
    cb.rgb_out[0] = (0xCu << 4);                     /* ab -> r0 */
    cb.final_abcd = 0x0000000C;                      /* D = r0 */
    st.combiner = &cb;
    static const uint32_t tx[1] = {0xFF804020};      /* A,R,G,B = FF,80,40,20 */
    st.tex[0].texels = tx; st.tex[0].width = 1; st.tex[0].height = 1;
    menu_vertex_out a = V(0, 0, 1, 1, 1, 1, 0.5f, 0.5f), b = V(8, 0, 1, 1, 1, 1, 0.5f, 0.5f), c = V(0, 8, 1, 1, 1, 1, 0.5f, 0.5f);
    menu_raster_triangle(&st, &a, &b, &c);
    assert(R(1, 1) == 0x80 && G(1, 1) == 0x40 && B(1, 1) == 0x20); /* white diffuse * texel */
}
static void test_scissor(void)
{
    setup(MENU_BLEND_OPAQUE);
    st.clip_x0 = 3; st.clip_y0 = 0; st.clip_x1 = 7; st.clip_y1 = 7;
    menu_vertex_out a = V(0, 0, 1, 0, 0, 1, 0, 0), b = V(8, 0, 1, 0, 0, 1, 0, 0), c = V(0, 8, 1, 0, 0, 1, 0, 0);
    menu_raster_triangle(&st, &a, &b, &c);
    assert(R(1, 1) == 0);
    assert(R(4, 1) == 255);
}

static void test_depth_test(void)
{
    /* LESS against a Z24S8 surface: a near triangle writes, a farther one is rejected. */
    static uint32_t zbuf[8 * 8];
    setup(MENU_BLEND_OPAQUE);
    for (int i = 0; i < 8 * 8; ++i) zbuf[i] = 0xFFFFFF00u | 0x5A;   /* cleared far, stencil 0x5A */
    st.depth.pixels = zbuf; st.depth.width = 8; st.depth.height = 8; st.depth.pitch = 8 * 4;
    st.depth.func = 0x201; st.depth.write = 1;
    menu_vertex_out a = V(0, 0, 1, 0, 0, 1, 0, 0), b = V(8, 0, 1, 0, 0, 1, 0, 0), c = V(0, 8, 1, 0, 0, 1, 0, 0);
    a.z = b.z = c.z = 0.25f;
    menu_raster_triangle(&st, &a, &b, &c);
    assert(R(1, 1) == 255);
    assert((zbuf[1 * 8 + 1] >> 8) == (uint32_t)(0.25f * 16777215.0f + 0.5f) && (zbuf[1 * 8 + 1] & 0xFF) == 0x5A);
    menu_vertex_out d = V(0, 0, 0, 1, 0, 1, 0, 0), e = V(8, 0, 0, 1, 0, 1, 0, 0), f = V(0, 8, 0, 1, 0, 1, 0, 0);
    d.z = e.z = f.z = 0.75f;                          /* farther: fails LESS, leaves red */
    menu_raster_triangle(&st, &d, &e, &f);
    assert(R(1, 1) == 255 && G(1, 1) == 0);
    st.depth.write = 0; d.z = e.z = f.z = 0.1f;       /* nearer, no write: colour updates, z kept */
    menu_raster_triangle(&st, &d, &e, &f);
    assert(G(1, 1) == 255 && (zbuf[1 * 8 + 1] >> 8) == (uint32_t)(0.25f * 16777215.0f + 0.5f));
}
static void test_blend_func_additive(void)
{
    setup(MENU_BLEND_FUNC);
    st.sfactor = 1; st.dfactor = 1; st.equation = 0x8006;   /* ONE, ONE, ADD */
    for (int i = 0; i < 8 * 8; ++i) buf[i * 4 + 0] = 100;   /* blue 100 */
    menu_vertex_out a = V(0, 0, 0.2f, 0, 0.2f, 1, 0, 0), b = V(8, 0, 0.2f, 0, 0.2f, 1, 0, 0), c = V(0, 8, 0.2f, 0, 0.2f, 1, 0, 0);
    menu_raster_triangle(&st, &a, &b, &c);
    assert(R(1, 1) == 51 && B(1, 1) == 151);
    assert(menu_raster_blend_supported(0x302, 0x303, 0x8006) && !menu_raster_blend_supported(0x9999, 1, 0x8006));
}
static void test_alpha_test(void)
{
    setup(MENU_BLEND_OPAQUE);
    st.alpha_test = 1; st.alpha_func = 0x204; st.alpha_ref = 0.5f;   /* GREATER than 0.5 */
    menu_vertex_out a = V(0, 0, 1, 0, 0, 0.25f, 0, 0), b = V(8, 0, 1, 0, 0, 0.25f, 0, 0), c = V(0, 8, 1, 0, 0, 0.25f, 0, 0);
    menu_raster_triangle(&st, &a, &b, &c);
    assert(R(1, 1) == 0);                             /* rejected */
    a.color[3] = b.color[3] = c.color[3] = 0.75f;
    menu_raster_triangle(&st, &a, &b, &c);
    assert(R(1, 1) == 255);                           /* passes */
}

static void test_texel_coords(void)
{
    /* A 4x1 linear image sampled at u=2.5 must return texel 2 (texel units), not wrap. */
    setup(MENU_BLEND_OPAQUE);
    static menu_combiner cb;
    memset(&cb, 0, sizeof cb);
    cb.stages = 1; cb.rgb_in[0] = (0x08u << 24) | (0x04u << 16); cb.rgb_out[0] = (0xCu << 4); cb.final_abcd = 0x0000000C;
    st.combiner = &cb;
    static const uint32_t tx[4] = {0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFFFF0000};
    st.tex[0].texels = tx; st.tex[0].width = 4; st.tex[0].height = 1; st.tex[0].texel_coords = 1;
    menu_vertex_out a = V(0, 0, 1, 1, 1, 1, 2.5f, 0.5f), b = V(8, 0, 1, 1, 1, 1, 2.5f, 0.5f), c = V(0, 8, 1, 1, 1, 1, 2.5f, 0.5f);
    menu_raster_triangle(&st, &a, &b, &c);
    assert(G(1, 1) == 255 && R(1, 1) == 0 && B(1, 1) == 0);
    st.tex[0].texel_coords = 0;                       /* normalized: 2.5 wraps to 0.5 -> texel 2 as well */
    a.uv[0][0] = b.uv[0][0] = c.uv[0][0] = 2.5f;
    setup(MENU_BLEND_OPAQUE); st.combiner = &cb; st.tex[0].texels = tx; st.tex[0].width = 4; st.tex[0].height = 1;
    a.uv[0][0] = b.uv[0][0] = c.uv[0][0] = 1.3f;      /* wraps to 0.3 -> texel 1 (blue) */
    menu_raster_triangle(&st, &a, &b, &c);
    assert(B(1, 1) == 255 && G(1, 1) == 0);
}
static void test_unused_unit_is_zero(void)
{
    /* stage 0: r0 = t1 * v0 (AB) + t0 * v0 (CD) with t1 unused: alpha must be t0.a * v0.a, not saturated */
    setup(MENU_BLEND_OPAQUE);
    static menu_combiner cb;
    memset(&cb, 0, sizeof cb);
    cb.stages = 1;
    cb.rgb_in[0] = (0x09u << 24) | (0x04u << 16) | (0x08u << 8) | 0x04u;   /* A=t1 B=v0 C=t0 D=v0 */
    cb.alpha_in[0] = (0x19u << 24) | (0x14u << 16) | (0x18u << 8) | 0x14u; /* same, alpha channel */
    cb.rgb_out[0] = 0xCu << 8; cb.alpha_out[0] = 0xCu << 8;                /* sum -> r0 */
    cb.final_abcd = 0x0000000C; cb.final_efg = 0x00001C00;                  /* rgb = r0, alpha = r0.a */
    st.combiner = &cb;
    static const uint32_t tx[1] = {0x40FFFFFF};                              /* white, alpha 0x40 */
    st.tex[0].texels = tx; st.tex[0].width = 1; st.tex[0].height = 1;
    menu_vertex_out a = V(0, 0, 1, 1, 1, 1, 0.5f, 0.5f), b = V(8, 0, 1, 1, 1, 1, 0.5f, 0.5f), c = V(0, 8, 1, 1, 1, 1, 0.5f, 0.5f);
    menu_raster_triangle(&st, &a, &b, &c);
    assert(R(1, 1) == 255 && buf[(1 * 8 + 1) * 4 + 3] == 0x40);            /* alpha came from t0 only */
}
static void test_depth_tolerance(void)
{
    /* EQUAL passes within a few 24-bit units (multipass jitter), fails beyond. */
    assert(depth_pass(0x202, 1000, 1010) && !depth_pass(0x202, 1000, 1100));
    assert(depth_pass(0x203, 1010, 1000) && !depth_pass(0x203, 1100, 1000));
    assert(depth_pass(0x201, 999, 1000) && !depth_pass(0x201, 1000, 1000));
}

static void test_face_culling(void)
{
    menu_vertex_out a = V(0, 0, 1, 0, 0, 1, 0, 0);
    menu_vertex_out b = V(8, 0, 1, 0, 0, 1, 0, 0);
    menu_vertex_out c = V(0, 8, 1, 0, 0, 1, 0, 0);
    const unsigned faces[] = {0x404, 0x405, 0x408};
    for (unsigned enable = 0; enable < 2; ++enable)
    for (unsigned ccw_front = 0; ccw_front < 2; ++ccw_front)
    for (unsigned f = 0; f < 3; ++f)
    for (unsigned reverse = 0; reverse < 2; ++reverse) {
        setup(MENU_BLEND_OPAQUE);
        st.cull_enable = enable; st.cull_face = faces[f];
        st.front_face = 0x900 + ccw_front;
        st.zpass_count = 1; menu_raster_zpass = 0;
        uint32_t depth[64]; for (unsigned i = 0; i < 64; ++i) depth[i] = 0xffffff5a;
        st.depth = (menu_depth){depth, 8, 8, 32, 0x201, 1};
        int front = reverse == ccw_front;
        int discarded = enable && (f == 2 || (f == 0 ? front : !front));
        menu_raster_triangle(&st, &a, reverse ? &c : &b, reverse ? &b : &c);
        assert(R(1, 1) == (discarded ? 0 : 255));
        assert(depth[9] == (discarded ? 0xffffff5a : 0x5a));
        assert((menu_raster_zpass == 0) == discarded);
        if (discarded) {
            for (unsigned i = 0; i < sizeof buf; ++i) assert(buf[i] == 0);
            for (unsigned i = 0; i < 64; ++i) assert(depth[i] == 0xffffff5a);
        }
    }
}

int main(void)
{
    test_face_culling();
    test_texel_coords();
    test_unused_unit_is_zero();
    test_depth_tolerance();
    test_flat_coverage();
    test_color_interpolation();
    test_alpha_blend();
    test_combiner_texture();
    test_scissor();
    test_depth_test();
    test_blend_func_additive();
    test_alpha_test();
    printf("menu_raster_test: all assertions passed\n");
    return 0;
}
