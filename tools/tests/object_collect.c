#define _POSIX_C_SOURCE 200809L
#include "xv_x86rt.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* Virtual world and stack pages stay clear of the physical pages used by the
 * identity-mapped image globals, the visited table and the jump table. Virtual
 * page ALIAS can share a physical page with the walk's stack frame. */
enum { ARENA = 8 << 20, PAGES = ARENA >> 12, WORLD = 0x500000, STACK = 0x680000,
       ALIAS = 0x4F0, EVENTS = 32768, NONE = 0xFFFFFFFFu };
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
void reference_00171F10(xctx *), candidate_00171F10(xctx *);
void xv_object_collect_override(int);
int xv_object_collect_enabled(void);
void xv_object_collect_report(unsigned);
#ifndef XV_NATIVE_OBJECT_COLLECT_DEFAULT
#define XV_NATIVE_OBJECT_COLLECT_DEFAULT 0
#endif
static unsigned startup_mode, override_calls;
void __real_xv_object_collect_override(int);
void __wrap_xv_object_collect_override(int value)
{
    assert(!startup_mode); /* A startup fixture must never change a live mode. */
    override_calls++;
    __real_xv_object_collect_override(value);
}

static char log_line[256];
void xk_os_log(const char *fmt, ...)
{
    va_list list;
    va_start(list, fmt);
    vsnprintf(log_line, sizeof log_line, fmt, list);
    va_end(list);
}

static unsigned report(unsigned *objects)
{
    unsigned walks, leaves, skipped;
    xv_object_collect_report(0);
    assert(sscanf(log_line, "[object-collect] %*u frames walks %u leaves %u objects %u skipped %u",
                  &walks, &leaves, objects, &skipped) == 4);
    (void)walks; (void)leaves;
    return skipped;
}

static uint32_t random_state = 1716415;
static uint32_t random_word(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

typedef struct { uint32_t site; uint64_t context, frame; } Event;
static Event events[2][EVENTS];
static unsigned event_count[2], lane;
/* Per case: the walk's ESP, valid object handles and the regression kind. */
static uint32_t walk_esp, datum_salt;
static unsigned object_total, regression, case_alias;
static unsigned yields_inner_none[2], yields_inner_datum[2], yields_outer[2];

static uint64_t hash_bytes(uint64_t h, const void *data, size_t size)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < size; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

/* Every translated call and yield must see the same complete context, its
 * own return word and arguments, and the walk's entire call-frame window. */
static uint64_t record(xctx *c, uint32_t site, unsigned words)
{
    if (event_count[lane] >= EVENTS) {
        fprintf(stderr, "event overflow: lane %u regression %u alias %u site %08X eax %08X edx %08X esi %08X\n",
                lane, regression, case_alias, site, c->r[0], c->r[2], c->r[6]);
        abort();
    }
    uint64_t context = hash_bytes(0xcbf29ce484222325ull, c, sizeof *c);
    uint64_t frame = 0xcbf29ce484222325ull;
    for (unsigned i = 0; i <= words; i++) {
        uint32_t word = X_M32(c->r[4] + i * 4u);
        frame = hash_bytes(frame, &word, sizeof word);
    }
    for (uint32_t a = walk_esp - 0xA0u; a != walk_esp + 0x20u; a++) {
        uint8_t byte = X_M8(a);
        frame = hash_bytes(frame, &byte, 1);
    }
    events[lane][event_count[lane]++] = (Event){site, context, frame};
    return context ^ (frame * 31u) ^ site;
}

/* Model callee outputs later read from 0x1716F0's own locals. */
static void fill(uint32_t from, uint32_t to, uint64_t seed)
{
    for (uint32_t a = from; a < to; a++) X_M8(a) = (uint8_t)(seed >> (a & 31u)) ^ (uint8_t)a;
}

static void finish(xctx *c, uint64_t h, unsigned bytes)
{
    /* Callee-saved registers survive. Scratch registers, flags and a vacant
     * x87 slot change, so the walk must reload what the original reloads. */
    c->r[0] = (uint32_t)h;
    c->r[1] = (uint32_t)(h >> 32);
    c->r[2] = (uint32_t)(h >> 16);
    c->f_kind = XK_LOGIC;
    c->f_op1 = c->f_op2 = 0;
    c->f_res = (uint32_t)(h >> 8);
    c->f_bits = 32;
    c->f_cf_override = c->f_of_override = 0;
    c->f_cf = (h >> 3) & 1u;
    c->f_of = (h >> 5) & 1u;
    c->st[(c->fsp - 1u) & 7u] = (double)(h & 0xFFFFu) / 7.0;
    c->fsw = (uint16_t)((c->fsw & ~0x4700u) | ((h >> 20) & 0x4500u));
    c->r[4] += 4u + bytes;
}

static uint32_t leaf_list[320];
static unsigned leaf_count, bsp_result;

void f_00088110(xctx *c)
{
    uint64_t h = record(c, 0x88110, 2);
    uint32_t out = c->r[6];
    X_M32(out + 0xC0Cu) = leaf_count;
    for (unsigned i = 0; i < leaf_count; i++) X_M32(out + 0xC10u + i * 4u) = leaf_list[i];
    finish(c, h, 8);
    X_R8L(0) = (uint8_t)bsp_result;
}
void f_000868F0(xctx *c) { finish(c, record(c, 0x868F0, 5), 0x14); }
void f_000855F0(xctx *c) { finish(c, record(c, 0x855F0, 6), 0x18); }
void f_00081770(xctx *c) { finish(c, record(c, 0x81770, 5), 0x14); }
void f_00172F40(xctx *c) { finish(c, record(c, 0x172F40, 5), 0x14); }
void f_000487E0(xctx *c)
{
    uint64_t h = record(c, 0x487E0, 1);
    fill(c->r[4] + 0x1Cu, c->r[4] + 0x7Cu, h);
    finish(c, h, 4);
}
void f_00081900(xctx *c)
{
    uint64_t h = record(c, 0x81900, 1);
    fill(c->r[4] + 0x1Cu, c->r[4] + 0x7Cu, h);
    finish(c, h, 4);
}
void f_00172DE0(xctx *c)
{
    uint64_t h = record(c, 0x172DE0, 0);
    fill(c->r[4] + 0x18u, c->r[4] + 0x78u, h);
    finish(c, h, 0);
}

/* Yield callbacks may change scratch registers and carry/overflow. Resumed
 * loops must keep the branch taken before the yield and reload afterwards.
 * Both object-chain back edges (walk and callee siblings) compare with -1;
 * the walk's leaf back edge compares with the leaf count. */
void __wrap_xv_preempt(xctx *c)
{
    uint64_t h = record(c, NONE, 0);
    c->preempt = 1 + (int32_t)(h % 40u);
    /* Regression 1 isolates frame aliasing; 2 isolates the resume branch. */
    unsigned policy = regression == 2 ? 1u : regression == 1 ? 0u : (unsigned)(h >> 40) % 4u;
    if (!policy) return;
    c->f_cf = (h >> 11) & 1u;
    c->f_of = (h >> 13) & 1u;
    c->r[1] = (uint32_t)(h >> 17);
    if (c->f_kind == XK_SUB && c->f_op2 == NONE) {
        if (policy == 1) {
            c->r[0] = NONE;
            yields_inner_none[lane]++;
        } else {
            c->r[0] = (uint32_t)(h >> 23);
            if (policy == 3 && object_total) {
                c->r[2] = (uint32_t)(h % object_total) | datum_salt << 16;
                yields_inner_datum[lane]++;
            }
        }
    } else {
        /* A different, valid leaf index; EDX is reloaded at 0x172047. */
        c->r[0] = (uint32_t)(h % (leaf_count ? leaf_count : 1u));
        c->r[2] = (uint32_t)(h >> 29);
        yields_outer[lane]++;
    }
}
void __wrap_xv_trap(xctx *c, uint32_t eip)
{
    fprintf(stderr, "trap %08X esp %08X\n", eip, c->r[4]);
    abort();
}

static void put_float(uint32_t address, float value) { x_guest_write(address, &value, 4); }
static float grid(int span) { return (float)((int)(random_word() % (unsigned)(2 * span + 1)) - span) * 0.5f; }
static uint32_t below_walk(unsigned low, unsigned high)
{
    return walk_esp - (low + (random_word() % ((high - low) / 4u + 1u)) * 4u);
}

/* One synthetic world: clusters with ordered reference chains, shared and
 * previously stamped objects, children, all early-exit classes, odd floats,
 * and inputs aliasing the walk's call frame virtually or physically.
 * Regression 1 places the query center at walk-0x80, where 0x1716FB stores
 * the flags before the center is read, without register-changing yields.
 * Regression 2 starts with a one-back-edge budget, has no frame aliases and
 * sets EAX to -1 at every chain back-edge yield. */
static uint32_t build(xctx *c)
{
    memset(g_xram, 0x5a, ARENA + 8u);
    /* Unmapped handles resolve here; -1 fields end their chains quickly. */
    memset(g_xram + (PAGES - 1u) * 4096u, 0xFF, 4096u + 8u);
    g_xpt[ALIAS] = (ALIAS ^ 1u) * 4096u;
    uint32_t entry = STACK + ((random_word() % 4096u) & ~3u);
    walk_esp = entry - 0x1024u;
    /* Caller locals above the walk hold -1, so frame-aliased object headers
     * below the walk end their child and sibling lists there. */
    for (uint32_t a = walk_esp + 0x14u; a != walk_esp + 0x200u; a++) X_M8(a) = 0xFF;
    unsigned alias = regression == 1 ? 1u : regression == 2 ? 0u : random_word() % 16u;
    case_alias = alias;

    uint32_t w = WORLD + (random_word() % 1024u) * 4u;
    uint32_t bsp = w, leaves = w + 0x100u, heads = w + 0x2000u;
    uint32_t ref_header = w + 0x3000u, ref_data = w + 0x3100u;
    uint32_t flag_block = w + 0x6F00u, center = w + 0x6F80u, packet = w + 0x6FC0u;
    uint32_t object_header = w + 0x7000u, object_data = w + 0x7100u;
    unsigned clusters = 1 + random_word() % 12u, top = random_word() % 160u;
    unsigned children = random_word() % 24u;
    if (regression) {
        top = 2 + top % 40u;
        children = 0;
    }
    unsigned total = top + children;
    unsigned leaves_in_bsp = 1 + random_word() % 320u;
    uint16_t salt = (uint16_t)(random_word() % 0xFFFFu);
    datum_salt = salt;
    object_total = total;
    #define DATUM(i) ((uint32_t)(i) | (uint32_t)salt << 16)

    uint32_t epoch = random_word(), object_epoch = random_word();
    X_IMG32(0x39BE58u) = bsp;
    X_M32(bsp + 0xE4u) = leaves;
    for (unsigned i = 0; i < leaves_in_bsp; i++)
        X_M16(leaves + i * 16u + 8u) = (uint16_t)(random_word() % clusters);
    X_IMG32(0x2D2FACu) = epoch;
    for (unsigned k = 0; k < clusters; k++)
        X_M32(0x2D2FB0u + k * 4u) = !regression && random_word() % 4u == 0 ? epoch + 1u : random_word();
    X_IMG32(0x2FC6A0u) = heads;
    X_IMG32(0x2FC6A4u) = ref_header;
    X_M32(ref_header + 0x34u) = ref_data;
    unsigned used = 0;
    for (unsigned k = 0; k < clusters; k++) {
        unsigned length = top ? random_word() % 40u : 0;
        if (regression && length < 2) length = 2;
        if (used + length > 400u) length = 400u - used;
        X_M32(heads + k * 4u) = length ? DATUM(used) : NONE;
        for (unsigned j = 0; j < length; j++, used++) {
            uint32_t link = ref_data + used * 12u;
            X_M32(link) = random_word();
            X_M32(link + 4u) = regression || random_word() % 50u ? DATUM(random_word() % top) : NONE;
            X_M32(link + 8u) = j + 1 < length ? DATUM(used + 1) : NONE;
        }
    }
    X_IMG32(0x2FC6ACu) = object_header;
    X_M32(object_header + 0x34u) = object_data;
    static const int types[] = {0,1,2,3,4,5,6,7,8,9,10,11,0,1,6,23,-8,-1,0x7FFF};
    static const uint32_t odd[] = {0x7F800000u, 0xFF800000u, 0x7FC00000u, 0x7F7FFFFFu, 0x00000001u};
    for (unsigned i = 0; i < total; i++) {
        uint32_t object = w + 0x10000u + i * 0x440u;
        X_M32(object_data + i * 12u + 8u) = object;
        for (uint32_t a = object; a < object + 0x430u; a += 4) X_M32(a) = random_word();
        uint32_t word = random_word();
        if (regression || random_word() % 8u) word &= ~1u;
        if (regression || random_word() % 8u) word &= ~0x1000000u;
        X_M32(object + 4u) = word;
        X_M32(object + 8u) = regression || random_word() % 6u ? random_word() : object_epoch + 1u;
        for (unsigned axis = 0; axis < 3; axis++) put_float(object + 0x50u + axis * 4u, grid(10));
        put_float(object + 0x5Cu, (float)(random_word() % 9u) * 0.5f);
        if (!regression && random_word() % 40u == 0)
            X_M32(object + 0x50u + (random_word() % 4u) * 4u) = odd[random_word() % 5u];
        X_M16(object + 0x64u) = (uint16_t)(regression ? 9 : types[random_word() % (sizeof types / sizeof *types)]);
        X_M8(object + 0xB6u) = (uint8_t)random_word();
        uint32_t sibling = NONE, child = NONE;
        if (i >= top && i + 1 < total && random_word() % 3u) sibling = DATUM(i + 1);
        if (i < top && children && random_word() % 10u == 0) sibling = DATUM(top + random_word() % children);
        /* Children form a DAG through higher indices; keep path counts small. */
        if (children && random_word() % 16u == 0) {
            unsigned first = i < top ? top : i + 1;
            if (first < total) child = DATUM(first + random_word() % (total - first));
        }
        X_M32(object + 0xC4u) = sibling;
        X_M32(object + 0xC8u) = child;
    }
    /* Frame aliases: the center or an object header, virtually or through a
     * shared physical page. A table pointer in the frame is not generated:
     * the original then follows frame words as handles and need not end. */
    if (alias == 1) center = regression == 1 ? walk_esp - 0x80u : below_walk(0x04u, 0x98u);
    if (alias == 2) {
        uint32_t target = below_walk(0x04u, 0x98u);
        g_xpt[ALIAS] = g_xpt[target >> 12];
        center = ALIAS * 4096u + (target & 4095u);
    }
    if (alias == 3 && top) {
        uint32_t object = below_walk(0x10u, 0xB0u);
        X_M32(object_data + (random_word() % top) * 12u + 8u) = object;
    }
    if (alias == 4 && top) {
        uint32_t target = below_walk(0x10u, 0xB0u);
        g_xpt[ALIAS] = g_xpt[target >> 12];
        X_M32(object_data + (random_word() % top) * 12u + 8u) = ALIAS * 4096u + (target & 4095u);
    }
    static const uint8_t selector[9] = {0,1,2,2,2,2,1,1,1};
    for (unsigned t = 0; t < 9; t++)
        X_M8(0x171944u + t) = random_word() % 10u ? selector[t] : (uint8_t)(random_word() % 3u);
    put_float(0x1F0F38u, 0.5f);
    X_IMG32(0x278248u) = flag_block;
    X_IMG32(0x2FC684u) = object_epoch;
    X_IMG32(0x39BE54u) = random_word();
    X_IMG32(0x27824Cu) = random_word();
    X_IMG16(0x1F845Cu) = (uint16_t)(random_word() % 64u);
    if (alias != 1 && alias != 2)
        for (unsigned axis = 0; axis < 3; axis++) put_float(center + axis * 4u, grid(10));
    X_M32(packet) = random_word();

    leaf_count = random_word() % 9u == 0 ? 0 : random_word() % 300u;
    if (regression && !leaf_count) leaf_count = 1;
    for (unsigned i = 0; i < leaf_count; i++)
        leaf_list[i] = (random_word() % leaves_in_bsp) | (random_word() & 0x80000000u);
    bsp_result = regression || random_word() % 8u != 0;

    uint32_t flags = random_word();
    if (regression || random_word() % 8u) flags |= 0x80u;
    if (regression == 1) flags = (flags & ~0x200u) | 0x40000000u;
    if (!regression && random_word() % 5u == 0) flags &= ~0xFFF00u;
    X_M32(entry) = random_word();
    X_M32(entry + 4u) = flags;
    X_M32(entry + 8u) = center;
    put_float(entry + 12u, (float)(random_word() % 13u) * 0.5f);
    X_M32(entry + 16u) = random_word();
    X_M32(entry + 20u) = random_word();
    X_M32(entry + 24u) = top && random_word() % 6u == 0 ? DATUM(random_word() % top) : random_word();
    X_M32(entry + 28u) = packet;
    #undef DATUM

    for (unsigned i = 0; i < sizeof *c; i++) ((uint8_t *)c)[i] = (uint8_t)random_word();
    c->r[4] = entry;
    c->f_kind = random_word() % 6u;
    c->f_bits = (unsigned[]){8, 16, 32}[random_word() % 3u];
    c->f_cf_override &= 1u;
    c->f_of_override &= 1u;
    c->f_cf &= 1u;
    c->f_of &= 1u;
    c->fsp &= 7u;
    c->fcw = 0x037F;
    c->preempt = regression == 2 ? 1 : (int32_t)(random_word() % 80u) - 2;
    return entry;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const char *mode = argv[1];
    unsigned cases = (unsigned)atoi(argv[2]);
    assert(cases >= 64);
    /* Negative controls may select one regression kind for every case. */
    const char *only = getenv("OBJECT_COLLECT_REGRESSION");
    int enabled;
    startup_mode = !strncmp(mode, "startup", 7);
    if (startup_mode) {
        if (!strcmp(mode, "startup")) {
            unsetenv("XV_NATIVE_OBJECT_COLLECT");enabled=XV_NATIVE_OBJECT_COLLECT_DEFAULT;
        } else {
            const char *setting = !strcmp(mode, "startup-env-0") ? "0" :
                !strcmp(mode, "startup-env-1") ? "1" :
                !strcmp(mode, "startup-env-empty") ? "" : NULL;
            assert(setting);assert(!setenv("XV_NATIVE_OBJECT_COLLECT",setting,1));
            enabled=atoi(setting)!=0;
        }
        /* Same passive resolution used by the post-dashboard startup log. */
        assert(xv_object_collect_enabled()==enabled);
    } else {
        assert(!strcmp(mode,"on")||!strcmp(mode,"off")||!strcmp(mode,"default")||!strcmp(mode,"environment"));
        enabled = !strcmp(mode, "on") || !strcmp(mode, "environment") ||
            (!strcmp(mode,"default") && XV_NATIVE_OBJECT_COLLECT_DEFAULT);
        if (!strcmp(mode, "environment")) setenv("XV_NATIVE_OBJECT_COLLECT", "1", 1);
        else unsetenv("XV_NATIVE_OBJECT_COLLECT");
        xv_object_collect_override(!strcmp(mode, "on") ? 1 : !strcmp(mode, "off") ? 0 : -1);
    }
    /* Unguarded 4-byte guest accesses may run past the last physical page. */
    g_xram = malloc(ARENA + 8u);
    g_img_base = g_xram;
    g_xpt = malloc((1u << 20) * sizeof *g_xpt);
    uint8_t *initial = malloc(ARENA + 8u), *expected = malloc(ARENA + 8u);
    assert(g_xram && g_xpt && initial && expected);
    for (unsigned i = 0; i < 1u << 20; i++)
        g_xpt[i] = (i < PAGES ? (i ^ 1u) : PAGES - 1u) * 4096u;
    uint64_t events_total = 0;
    unsigned objects_total = 0, skipped_total = 0, alias_skips = 0, objects;
    for (unsigned k = 0; k < cases; k++) {
        /* The first cases are the review's two regressions. */
        regression = only ? (unsigned)atoi(only) : k < 16 ? 1u : k < 32 ? 2u : 0u;
        xctx start;
        uint32_t entry = build(&start);
        (void)entry;
        memcpy(initial, g_xram, ARENA + 8u);
        xctx reference = start, candidate = start;
        lane = 0; event_count[0] = 0;
        reference_00171F10(&reference);
        memcpy(expected, g_xram, ARENA + 8u);
        memcpy(g_xram, initial, ARENA + 8u);
        report(&objects);
        lane = 1; event_count[1] = 0;
        candidate_00171F10(&candidate);
        unsigned skipped = report(&objects);
        objects_total += objects;
        skipped_total += skipped;
        if (regression == 1) alias_skips += skipped;
        unsigned i = 0;
        while (i < event_count[0] && i < event_count[1] && events[0][i].site == events[1][i].site &&
               events[0][i].context == events[1][i].context && events[0][i].frame == events[1][i].frame)
            i++;
        if (i != event_count[0] || i != event_count[1]) {
            fprintf(stderr, "case %u event %u of %u/%u: site %08X/%08X\n", k, i,
                    event_count[0], event_count[1], i < event_count[0] ? events[0][i].site : 0,
                    i < event_count[1] ? events[1][i].site : 0);
            abort();
        }
        if (memcmp(&reference, &candidate, sizeof reference)) {
            for (unsigned i = 0; i < sizeof reference; i++)
                if (((uint8_t *)&reference)[i] != ((uint8_t *)&candidate)[i])
                    fprintf(stderr, "case %u context byte %u expected %02x got %02x\n", k, i,
                            ((uint8_t *)&reference)[i], ((uint8_t *)&candidate)[i]);
            abort();
        }
        /* No mask: every guest byte, including the skipped call frame. */
        if (memcmp(expected, g_xram, ARENA + 8u)) {
            for (unsigned i = 0, shown = 0; i < ARENA + 8u && shown < 16; i++)
                if (expected[i] != g_xram[i])
                    fprintf(stderr, "case %u physical %06X expected %02x got %02x\n",
                            k, i, expected[i], g_xram[i]), shown++;
            abort();
        }
        events_total += event_count[0];
    }
    assert(yields_inner_none[0] == yields_inner_none[1] && yields_outer[0] == yields_outer[1] &&
           yields_inner_datum[0] == yields_inner_datum[1]);
    if (!enabled) assert(objects_total == 0 && skipped_total == 0);
    else if (!only) assert(skipped_total > cases && objects_total - skipped_total > cases && alias_skips > 16);
    else if (atoi(only) == 1) assert(alias_skips > cases);
    else assert(skipped_total > 0);
    if (!only) assert(yields_inner_none[0] > 16 && yields_inner_datum[0] > 16 && yields_outer[0] > 16);
    else if (atoi(only) == 2) assert(yields_inner_none[0] > cases / 4);
    printf("PASS object collect %s: %u cases, %llu events, %u objects, %u skipped, "
           "%u frame-alias regression skips, yields -1/datum/leaf %u/%u/%u\n",
           mode, cases, (unsigned long long)events_total, objects_total, skipped_total,
           alias_skips, yields_inner_none[0], yields_inner_datum[0], yields_outer[0]);
    assert(override_calls==(startup_mode?0u:1u));
    free(expected); free(initial); free(g_xpt); free(g_xram);
    return 0;
}
