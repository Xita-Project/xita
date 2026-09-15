/* The menu geometry module assembles NV2A immediate/indexed emission into one
 * descriptor and only completes a draw through a backend that accepts it. A
 * rejected or unbacked draw returns 0 (state preserved); a non-geometry method
 * for an idle module returns -1 so ordinary state capture still runs. */
#include "menu_draw.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static h2_command_state state;
static h2_kelvin_clear clear;
static h2_menu_draw menu;
static h2_menu_request last;
static unsigned render_calls;
static int render_result;

static int mock_render(void *opaque, const h2_menu_request *r)
{
    assert(opaque == &render_calls);
    ++render_calls;
    last = *r;                 /* shallow copy; pointers stay valid for the test */
    return render_result;
}
static uint32_t bits(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }

static int method(uint16_t m, uint32_t v) { return h2_menu_method(&menu, &state, &clear, 0, m, v); }

static void reset(int with_render)
{
    memset(&menu, 0, sizeof menu);
    memset(&state, 0, sizeof state);
    memset(&clear, 0, sizeof clear);
    state.bound[0] = 1;         /* Kelvin bound on subchannel 0 */
    menu.render = with_render ? mock_render : NULL;
    menu.opaque = &render_calls;
    render_calls = 0; render_result = 1;
}

/* Emit one immediate vertex: current texcoord (attr3, 2S) then position
 * (attr0, 4F) whose final component submits the vertex. */
static void immediate_vertex(float x, float y)
{
    assert(method(0x190C, 0) == 1);            /* attr3 texcoord (0,0) */
    assert(method(0x1A00, bits(x)) == 1);      /* attr0.x */
    assert(method(0x1A04, bits(y)) == 1);      /* attr0.y */
    assert(method(0x1A08, bits(0.5f)) == 1);   /* attr0.z */
    assert(method(0x1A0C, bits(1.0f)) == 1);   /* attr0.w submits the vertex */
}

static void test_immediate_tri_fan(void)
{
    reset(1);
    assert(method(0x17FC, 7) == 1 && menu.active && menu.primitive == 7);
    assert(method(0x1964, 0xFFFFEECE) == 1);   /* attr9 diffuse, one value for the fan */
    const float xs[4] = {72.10f, 72.73f, 83.69f, 83.05f};
    const float ys[4] = {147.92f, 136.97f, 137.60f, 148.56f};
    for (unsigned v = 0; v < 4; ++v) immediate_vertex(xs[v], ys[v]);
    assert(menu.vertex_count == 4 && !menu.overflow);
    assert(method(0x17FC, 0) == 1);            /* END: renders */
    assert(!menu.active && render_calls == 1 && menu.completed == 1);
    assert(last.primitive == 7 && last.vertex_count == 4);
    assert(last.index_count == 0 && last.array_count == 0);
    for (unsigned v = 0; v < 4; ++v) {
        assert(last.vertices[v].attribute[0][0] == xs[v]);
        assert(last.vertices[v].attribute[0][1] == ys[v]);
        assert(last.vertices[v].attribute[0][3] == 1.0f);
    }
    /* diffuse persisted across all four vertices, unpacked B,G,R,A -> R,G,B,A */
    for (unsigned v = 0; v < 4; ++v) {
        assert(last.vertices[v].attribute[9][0] == 0xFF / 255.0f);
        assert(last.vertices[v].attribute[9][1] == 0xEE / 255.0f);
        assert(last.vertices[v].attribute[9][2] == 0xCE / 255.0f);
        assert(last.vertices[v].attribute[9][3] == 0xFF / 255.0f);
    }
}

static void test_indexed_triangles(void)
{
    reset(1);
    assert(method(0x17FC, 5) == 1);            /* TRIANGLES */
    assert(method(0x1800, 0x00030002) == 1);   /* two 16-bit indices: 2, 3 */
    assert(method(0x1800, 0x00050004) == 1);   /* 4, 5 */
    assert(method(0x17FC, 0) == 1);
    assert(render_calls == 1 && last.index_count == 4);
    assert(last.indices[0] == 2 && last.indices[1] == 3 &&
           last.indices[2] == 4 && last.indices[3] == 5);
    assert(last.vertex_count == 0);
}

static void test_draw_arrays(void)
{
    reset(1);
    assert(method(0x17FC, 6) == 1);            /* TRI_STRIP */
    assert(method(0x1810, (2u << 24) | 10u) == 1);  /* start 10, count 3 */
    assert(method(0x1810, (0u << 24) | 13u) == 1);  /* contiguous start 13, count 1 */
    assert(method(0x17FC, 0) == 1);
    assert(last.array_start == 10 && last.array_count == 4);
}

static void test_reject_without_backend(void)
{
    reset(0);                                   /* no render backend installed */
    assert(method(0x17FC, 7) == 1);            /* claims the draw */
    immediate_vertex(1.0f, 2.0f);
    assert(method(0x17FC, 0) == 0);            /* END rejects: nothing rendered */
    assert(!menu.active && menu.completed == 0 && menu.rejected == 1 && render_calls == 0);
}

static void test_backend_rejection_preserves(void)
{
    reset(1); render_result = 0;                /* backend declines the draw */
    assert(method(0x17FC, 7) == 1);
    immediate_vertex(1.0f, 2.0f);
    assert(method(0x17FC, 0) == 0);
    assert(render_calls == 1 && menu.completed == 0 && menu.rejected == 1);
}

static void test_not_mine_when_idle(void)
{
    reset(1);
    assert(method(0x1810, 0) == -1);           /* not a BEGIN: state capture handles it */
    assert(method(0x1800, 0) == -1);
    assert(method(0x17FC, 0) == -1);           /* END with no active draw is not ours */
    assert(method(0x17FC, 11) == -1);          /* out-of-range primitive */
    state.bound[0] = 0;
    assert(method(0x17FC, 7) == -1);           /* Kelvin not bound: not ours */
}

static void test_active_invariants(void)
{
    reset(1);
    assert(method(0x17FC, 7) == 1);
    state.bound[1] = 1;                         /* a second Kelvin subchannel is bound */
    assert(h2_menu_method(&menu, &state, &clear, 1, 0x1A00, 0) == 0); /* wrong subchannel */
    assert(method(0x17FC, 7) == 0);            /* nested BEGIN */
    assert(method(0x0300, 0) == 0);            /* state method mid-draw is invalid */
    assert(menu.active);
}

static void test_overflow_rejects(void)
{
    reset(1);
    assert(method(0x17FC, 5) == 1);
    for (unsigned i = 0; i <= H2_MENU_MAX_VERTICES; ++i) immediate_vertex(1.0f, 1.0f);
    assert(menu.overflow);
    assert(method(0x17FC, 0) == 0 && render_calls == 0);
}

int main(void)
{
    test_immediate_tri_fan();
    test_indexed_triangles();
    test_draw_arrays();
    test_reject_without_backend();
    test_backend_rejection_preserves();
    test_not_mine_when_idle();
    test_active_invariants();
    test_overflow_rejects();
    printf("menu_draw_test: all assertions passed\n");
    return 0;
}
