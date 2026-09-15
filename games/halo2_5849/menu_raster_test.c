/* Rasterizer coverage, interpolation, blend, texture and scissor. */
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
    st.blend = blend;
    st.clip_x0 = 0; st.clip_y0 = 0; st.clip_x1 = 7; st.clip_y1 = 7;
}
static uint8_t B(int x, int y) { return buf[(y * 8 + x) * 4 + 0]; }
static uint8_t G(int x, int y) { return buf[(y * 8 + x) * 4 + 1]; }
static uint8_t R(int x, int y) { return buf[(y * 8 + x) * 4 + 2]; }
static menu_vertex_out V(float x, float y, float r, float g, float b, float a, float u, float v)
{ menu_vertex_out o = {x, y, 0.0f, 1.0f, {r, g, b, a}, {u, v}}; return o; }

static void test_flat_coverage(void)
{
    setup(MENU_BLEND_OPAQUE);
    /* lower-left half of the 8x8: (0,0),(8,0),(0,8), solid red */
    menu_vertex_out a = V(0, 0, 1, 0, 0, 1, 0, 0), b = V(8, 0, 1, 0, 0, 1, 0, 0), c = V(0, 8, 1, 0, 0, 1, 0, 0);
    menu_raster_triangle(&st, &a, &b, &c);
    assert(R(1, 1) == 255 && G(1, 1) == 0 && B(1, 1) == 0); /* inside */
    assert(R(6, 6) == 0 && B(6, 6) == 0);                    /* outside the diagonal */
}
static void test_color_interpolation(void)
{
    setup(MENU_BLEND_OPAQUE);
    /* red at left verts, green at the far corner; midpoint blends */
    menu_vertex_out a = V(0, 0, 1, 0, 0, 1, 0, 0), b = V(8, 0, 0, 1, 0, 1, 0, 0), c = V(0, 8, 1, 0, 0, 1, 0, 0);
    menu_raster_triangle(&st, &a, &b, &c);
    /* near vertex b (x large) green dominates */
    assert(G(6, 0) > R(6, 0));
    /* near vertex a red dominates */
    assert(R(0, 1) > G(0, 1));
}
static void test_alpha_blend(void)
{
    setup(MENU_BLEND_ALPHA);
    for (int i = 0; i < 8 * 8; ++i) buf[i * 4 + 0] = 255; /* background blue */
    menu_vertex_out a = V(0, 0, 1, 0, 0, 0.5f, 0, 0), b = V(8, 0, 1, 0, 0, 0.5f, 0, 0), c = V(0, 8, 1, 0, 0, 0.5f, 0, 0);
    menu_raster_triangle(&st, &a, &b, &c);
    /* src red*1 + dst blue*(1-0.5): R~127, B~127 */
    assert(R(1, 1) >= 125 && R(1, 1) <= 129);
    assert(B(1, 1) >= 125 && B(1, 1) <= 129);
}
static void test_texture_modulate(void)
{
    setup(MENU_BLEND_OPAQUE);
    static const uint32_t tx[1] = {0xFF804020}; /* A,R,G,B = FF,80,40,20 */
    st.tex0.texels = tx; st.tex0.width = 1; st.tex0.height = 1;
    menu_vertex_out a = V(0, 0, 1, 1, 1, 1, 0.5f, 0.5f), b = V(8, 0, 1, 1, 1, 1, 0.5f, 0.5f), c = V(0, 8, 1, 1, 1, 1, 0.5f, 0.5f);
    menu_raster_triangle(&st, &a, &b, &c);
    assert(R(1, 1) == 0x80 && G(1, 1) == 0x40 && B(1, 1) == 0x20); /* white diffuse * texel */
}
static void test_scissor(void)
{
    setup(MENU_BLEND_OPAQUE);
    st.clip_x0 = 3; st.clip_y0 = 0; st.clip_x1 = 7; st.clip_y1 = 7; /* exclude x<3 */
    menu_vertex_out a = V(0, 0, 1, 0, 0, 1, 0, 0), b = V(8, 0, 1, 0, 0, 1, 0, 0), c = V(0, 8, 1, 0, 0, 1, 0, 0);
    menu_raster_triangle(&st, &a, &b, &c);
    assert(R(1, 1) == 0);   /* scissored out */
    assert(R(4, 1) == 255); /* inside scissor and triangle */
}

int main(void)
{
    test_flat_coverage();
    test_color_interpolation();
    test_alpha_blend();
    test_texture_modulate();
    test_scissor();
    printf("menu_raster_test: all assertions passed\n");
    return 0;
}
