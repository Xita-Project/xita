#include "menu_draw.h"
#include <string.h>

static float as_float(uint32_t value)
{ float f; memcpy(&f, &value, 4); return f; }

/* Decode one NV097_SET_VERTEX_DATA immediate method into the current-vertex
 * attribute store. Returns 1 if the method is an immediate attribute write and
 * sets *emit when it completes attribute 0 (the position), which submits a
 * vertex on real hardware. Persistent attributes keep their value across
 * vertices; only attribute 0 being rewritten assembles the next vertex. */
static int vertex_data(h2_menu_draw *q, uint16_t method, uint32_t value, int *emit)
{
    *emit = 0;
    if (method >= 0x1880 && method <= 0x18FC) { /* SET_VERTEX_DATA2F */
        unsigned attr = (method - 0x1880) / 8, comp = ((method - 0x1880) % 8) / 4;
        q->current[attr][comp] = as_float(value);
        if (attr == 0 && comp == 1) *emit = 1;
        return 1;
    }
    if (method >= 0x1900 && method <= 0x193C) { /* SET_VERTEX_DATA2S (packed) */
        unsigned attr = (method - 0x1900) / 4;
        q->current[attr][0] = (float)(int16_t)(value & 0xFFFF);
        q->current[attr][1] = (float)(int16_t)(value >> 16);
        q->current[attr][2] = 0.0f; q->current[attr][3] = 1.0f;
        if (attr == 0) *emit = 1;
        return 1;
    }
    if (method >= 0x1940 && method <= 0x197C) { /* SET_VERTEX_DATA4UB */
        unsigned attr = (method - 0x1940) / 4;
        static const unsigned shift[4] = {16, 8, 0, 24}; /* packed B,G,R,A -> R,G,B,A */
        for (unsigned k = 0; k < 4; ++k)
            q->current[attr][k] = ((value >> shift[k]) & 255) / 255.0f;
        if (attr == 0) *emit = 1;
        return 1;
    }
    if (method >= 0x1980 && method <= 0x19FC) { /* SET_VERTEX_DATA4S (two words) */
        unsigned attr = (method - 0x1980) / 8, half = ((method - 0x1980) % 8) / 4;
        q->current[attr][half * 2 + 0] = (float)(int16_t)(value & 0xFFFF);
        q->current[attr][half * 2 + 1] = (float)(int16_t)(value >> 16);
        if (attr == 0 && half == 1) *emit = 1;
        return 1;
    }
    if (method >= 0x1A00 && method <= 0x1A7C) { /* SET_VERTEX_DATA4F */
        unsigned attr = (method - 0x1A00) / 16, comp = ((method - 0x1A00) % 16) / 4;
        q->current[attr][comp] = as_float(value);
        if (attr == 0 && comp == 3) *emit = 1;
        return 1;
    }
    return 0;
}

static void push_vertex(h2_menu_draw *q)
{
    if (q->vertex_count >= H2_MENU_MAX_VERTICES) { q->overflow = 1; return; }
    memcpy(q->vertices[q->vertex_count].attribute, q->current, sizeof q->current);
    ++q->vertex_count;
    q->has_immediate = 1;
}

/* Capture indexed/array emission for the backend to fetch from guest arrays.
 * No guest memory is read here; only the command stream's own indices/ranges
 * are recorded. Returns 1 if the method was an emission method. */
static int emission(h2_menu_draw *q, uint16_t method, uint32_t value)
{
    if (method == 0x1800) { /* ARRAY_ELEMENT16: two 16-bit indices per word */
        for (unsigned k = 0; k < 2; ++k) {
            uint16_t index = (uint16_t)(value >> (16 * k));
            if (q->index_count >= H2_MENU_MAX_INDICES) { q->overflow = 1; return 1; }
            q->indices[q->index_count++] = index;
        }
        q->has_indexed = 1;
        return 1;
    }
    if (method == 0x1808) { /* ARRAY_ELEMENT32 */
        if (value > 0xFFFF) { q->overflow = 1; return 1; } /* backend uses 16-bit indices */
        if (q->index_count >= H2_MENU_MAX_INDICES) { q->overflow = 1; return 1; }
        q->indices[q->index_count++] = (uint16_t)value;
        q->has_indexed = 1;
        return 1;
    }
    if (method == 0x1810) { /* DRAW_ARRAYS: start in bits 0..23, count-1 in 24..31 */
        uint32_t start = value & 0x00FFFFFF, count = ((value >> 24) & 0xFF) + 1;
        if (!q->has_array) q->array_start = start;
        else if (start != q->array_start + q->array_count) { q->overflow = 1; return 1; }
        q->array_count += count;
        q->has_array = 1;
        return 1;
    }
    if (method == 0x1818) { /* INLINE_ARRAY: inline vertex words */
        ++q->inline_words;
        q->has_inline = 1;
        return 1;
    }
    return 0;
}

static int finish(h2_menu_draw *q, h2_command_state *s, h2_kelvin_clear *c)
{
    if (q->overflow || !q->render || q->completed == UINT64_MAX) return 0;
    h2_menu_request r;
    memset(&r, 0, sizeof r);
    r.primitive = q->primitive;
    r.vertex_count = q->vertex_count; r.vertices = q->vertices;
    r.index_count = q->index_count; r.indices = q->indices;
    r.array_start = q->array_start; r.array_count = q->array_count;
    r.inline_words = q->inline_words;
    r.state = s; r.clear = c;
    if (!q->render(q->opaque, &r)) return 0;
    ++q->completed;
    return 1;
}

int h2_menu_method(h2_menu_draw *q, h2_command_state *s, h2_kelvin_clear *c,
                   uint8_t sub, uint16_t method, uint32_t value)
{
    if (!q || !s || !c || sub >= 8 || s->bound[sub] != 1) return -1;
    if (!q->active) {
        if (method != 0x17FC || value == 0 || value > 10) return -1; /* not a draw start */
        memset(q->current, 0, sizeof q->current);
        for (unsigned a = 0; a < H2_MENU_ATTRIBUTES; ++a) q->current[a][3] = 1.0f;
        q->vertex_count = q->index_count = 0;
        q->array_start = q->array_count = q->inline_words = 0;
        q->has_immediate = q->has_indexed = q->has_array = q->has_inline = 0;
        q->overflow = 0; q->primitive = (uint16_t)value;
        q->active = 1; q->subchannel = sub;
        return 1;
    }
    if (sub != q->subchannel) return 0;
    if (method == 0x17FC) {
        if (value != 0) return 0; /* nested BEGIN without END is invalid */
        q->active = 0;
        if (finish(q, s, c)) return 1;
        ++q->rejected;
        return 0;
    }
    int emit = 0;
    if (vertex_data(q, method, value, &emit)) {
        if (emit) push_vertex(q);
        return 1;
    }
    if (emission(q, method, value)) return 1;
    /* A non-vertex, non-emission method inside the draw is interleaved state
     * (e.g. a per-batch clip rectangle). Return "not mine" so the dispatcher
     * routes it to command-state capture; the draw stays open. */
    return -1;
}
