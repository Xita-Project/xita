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

int main(void)
{
    test_flat_coverage();
    test_color_interpolation();
    test_alpha_blend();
    test_combiner_texture();
    test_scissor();
    printf("menu_raster_test: all assertions passed\n");
    return 0;
}
