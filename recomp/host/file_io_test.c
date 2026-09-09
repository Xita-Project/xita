/* Real NT I/O entry points over fragmented guest memory and a fake file. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/xk.h"
#ifdef __vita__
#include <psp2/kernel/clib.h>
#undef assert
#define assert(expr) do { if (!(expr)) { sceClibPrintf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)
#define puts(s) sceClibPrintf("%s\n", (s))
#endif

uint8_t *g_xram;
uint32_t *g_xpt;
xk_thread *xk_cur;
struct xk_file { unsigned char data[32768]; uint32_t size, calls, short_limit, fail_call; } disk;
static xk_obj file, event;
static unsigned apcs, signals;
static unsigned variant_reads;
static unsigned builtin_reads, builtin_preset;
/* Tag mutation has its own real-map integration test in tools/test_quality.py. */
void xk_quality_map_read(uint32_t address,uint32_t bytes)
{ assert(address==0x803A6000u && bytes); }
int xk_variant_recover_unsigned(uint32_t data) { assert(data == 0x1103); variant_reads++; return 0; }
int xk_builtin_profile_recover(uint32_t data, unsigned preset)
{ assert(data == 0x1103 && preset < 2); builtin_reads++; builtin_preset = preset; return 0; }
xk_obj *xk_handle_get(uint32_t h) { return h == 1 ? &file : h == 2 ? &event : NULL; }
xk_obj *xk_handle_get_type(uint32_t h, xk_objtype t) { xk_obj *o = xk_handle_get(h); return o && o->type == t ? o : NULL; }
void xk_signal_check(void) { signals++; }
void xk_apc_queue(xk_thread *t, uint32_t r, uint32_t a, uint32_t b, uint32_t c)
{ (void)t; assert(r == 99 && a == 88 && b == 0x300 && !c); apcs++; }
void xk_os_log(const char *fmt, ...) { (void)fmt; }
void xk_os_sleep_us(uint64_t us) { (void)us; }
int64_t xk_os_size(xk_file *f) { return f->size; }
int64_t xk_os_read(xk_file *f, uint64_t pos, void *buf, uint32_t n)
{
    if (++f->calls == f->fail_call) return -1;
    if (pos >= f->size) return 0;
    if (n > f->size - pos) n = f->size - pos;
    if (f->short_limit && n > f->short_limit) n = f->short_limit;
    memcpy(buf, f->data + pos, n); return n;
}
int64_t xk_os_write(xk_file *f, uint64_t pos, const void *buf, uint32_t n)
{
    if (++f->calls == f->fail_call) return -1;
    assert(pos + n <= sizeof f->data);
    if (f->short_limit && n > f->short_limit) n = f->short_limit;
    memcpy(f->data + pos, buf, n);
    if (pos + n > f->size) f->size = pos + n;
    return n;
}
static uint64_t reserved_size;
static int reserve_fail;
int xk_os_truncate(xk_file *f, uint64_t size)
{ assert(f == &disk); reserved_size = size; return reserve_fail ? -1 : 0; }
int xk_os_rename(const char *a, const char *b) { (void)a; (void)b; assert(0); return -1; }
void xk_ansi_to_c(uint32_t a, char *b, unsigned n) { (void)a; (void)b; (void)n; assert(0); }
extern void xk_NtSetInformationFile(xctx *);
static void check_reservation(uint32_t info, uint64_t size, uint32_t cls, int fail)
{
    xctx c = {0}; c.r[4] = 0x100;
    X_M32(0x104) = 1; X_M32(0x108) = 0x300; X_M32(0x10c) = info;
    X_M32(0x110) = 8; X_M32(0x114) = cls;
    X_M32(info) = (uint32_t)size; X_M32(info + 4) = (uint32_t)(size >> 32);
    reserved_size = ~size; reserve_fail = fail;
    xk_NtSetInformationFile(&c);
    assert(reserved_size == size && c.r[4] == 0x118);
    assert(c.r[0] == (fail ? STATUS_ACCESS_DENIED : STATUS_SUCCESS));
    assert(X_M32(0x300) == c.r[0]);
}
extern void xk_NtReadFile(xctx *);
extern void xk_NtWriteFile(xctx *);
static uint32_t run(int writing, uint32_t buf, uint32_t n, uint32_t pos)
{
    xctx c = {0}; c.r[4] = 0x100;
    X_M32(0x104) = 1; X_M32(0x108) = 2; X_M32(0x10c) = 99; X_M32(0x110) = 88;
    X_M32(0x114) = 0x300; X_M32(0x118) = buf; X_M32(0x11c) = n; X_M32(0x120) = 0x308;
    X_M64(0x308) = pos;
    if (writing) xk_NtWriteFile(&c); else xk_NtReadFile(&c);
    assert(c.r[4] == 0x124 && X_M32(0x300) == c.r[0]);
    assert(event.u.event.signaled && apcs == signals);
    return c.r[0];
}
int main(void)
{
    g_xram = malloc(32768); g_xpt = calloc(1u << 20, 4); assert(g_xram && g_xpt);
    /* Guest pages 1/2 are adjacent; page 3 lives beyond a guard page. */
    g_xpt[1] = 4096; g_xpt[2] = 8192; g_xpt[3] = 20480;
    memset(g_xram, 0xa5, 32768);
    file.type = XO_FILE; file.u.file.f = &disk; file.u.file.path = "test.bin";
    event.type = XO_EVENT;
    disk.size = sizeof disk.data;
    for (unsigned i = 0; i < disk.size; ++i) disk.data[i] = (unsigned char)(i * 17 + i / 256);
    assert(run(0, 0x1103, 10000, 7) == STATUS_SUCCESS && disk.calls == 2);
    assert(X_M32(0x304) == 10000 && file.u.file.pos == 10007);
    for (unsigned i = 0; i < 10000; ++i) assert(X_M8(0x1103 + i) == disk.data[7 + i]);
    for (unsigned i = 12288; i < 20480; ++i) assert(g_xram[i] == 0xa5);
    disk.calls = 0;
    assert(run(1, 0x1103, 10000, 12000) == STATUS_SUCCESS && disk.calls == 2);
    for (unsigned i = 0; i < 10000; ++i) assert(disk.data[12000 + i] == X_M8(0x1103 + i));
    disk.calls = 0;
    assert(run(0, 0x1103, 6000, 0) == STATUS_SUCCESS && disk.calls == 1);
    disk.calls = 0; disk.short_limit = 23; X_M8(0x1103 + 23) = 0xed;
    assert(run(0, 0x1103, 10000, 0) == STATUS_SUCCESS && X_M32(0x304) == 23 && disk.calls == 1);
    assert(X_M8(0x1103 + 23) == 0xed && file.u.file.pos == 23);
    assert(run(1, 0x1103, 10000, 0) == STATUS_SUCCESS && X_M32(0x304) == 23);
    disk.short_limit = 0; disk.calls = 0; disk.fail_call = 1;
    assert(run(0, 0x1103, 10000, 0) == STATUS_UNSUCCESSFUL && !X_M32(0x304));
    disk.calls = 0;
    assert(run(1, 0x1103, 10000, 0) == STATUS_ACCESS_DENIED && !X_M32(0x304));
    disk.calls = 0; disk.fail_call = 2;
    assert(run(0, 0x1103, 10000, 0) == STATUS_SUCCESS && X_M32(0x304) == 0x3000 - 0x1103);
    disk.fail_call = 0;
    assert(run(0, 0x1103, 1, disk.size) == STATUS_END_OF_FILE && !X_M32(0x304));
    assert(run(0, 0x1103, 0, 0) == STATUS_SUCCESS && !X_M32(0x304));
    assert(run(0, 0x1103, 512, 0) == STATUS_SUCCESS && !variant_reads);
    file.u.file.path = "/save/custom/blam.lst";
    assert(run(0, 0x1103, 512, 0) == STATUS_SUCCESS && variant_reads == 1);
    assert(run(0, 0x1103, 512, 512) == STATUS_SUCCESS && variant_reads == 1);
    assert(run(0, 0x1103, 124, 0) == STATUS_SUCCESS && variant_reads == 1);
    disk.short_limit = 124;
    assert(run(0, 0x1103, 512, 0) == STATUS_SUCCESS && variant_reads == 1);
    disk.short_limit = 0;
    file.u.file.path = "/save/profile/blam.sav";
    assert(run(0, 0x1103, 512, 0) == STATUS_SUCCESS && variant_reads == 1);
    assert(!builtin_reads);
    file.u.file.path = "/save/cache/saved/player_profiles/default_profile/00.sav";
    assert(run(0, 0x1103, 512, 0) == STATUS_SUCCESS && builtin_reads == 1 && builtin_preset == 0);
    assert(run(0, 0x1103, 512, 512) == STATUS_SUCCESS && builtin_reads == 1);
    assert(run(0, 0x1103, 48, 0) == STATUS_SUCCESS && builtin_reads == 1);
    disk.short_limit = 48;
    assert(run(0, 0x1103, 512, 0) == STATUS_SUCCESS && builtin_reads == 1);
    disk.short_limit = 0;
    file.u.file.path = "/save/cache/saved/player_profiles/default_profile/01.sav";
    assert(run(0, 0x1103, 512, 0) == STATUS_SUCCESS && builtin_reads == 2 && builtin_preset == 1);
    file.u.file.path = "/save/cache/saved/player_profiles/default_profile/02.sav";
    assert(run(0, 0x1103, 512, 0) == STATUS_SUCCESS && builtin_reads == 2);
    /* Only the two built-in index entries become selectable. Split the valid
     * flag onto a noncontiguous guest page; never rewrite the cache on disk. */
    file.u.file.path = "/save/cache/saved/hdmu.map";
    for (unsigned test = 0; test < 8; ++test) {
        memset(disk.data, 0, 518);
        strcpy((char *)disk.data, test == 1 ? "z:\\saved\\player_profiles\\default_profile\\01.sav" :
            test == 2 ? "z:\\saved\\player_profiles\\default_profile\\02.sav" :
            test == 3 ? "u:\\user\\blam.sav" : "z:\\saved\\player_profiles\\default_profile\\00.sav");
        disk.data[516] = test == 4 ? 0 : 1;
        disk.data[512] = test == 5 ? 2 : 0;
        disk.data[517] = test == 6 ? 1 : 0;
        if (test == 7) memset(disk.data, 'x', 256);
        assert(run(0, 0x2dfb, 518, 0) == STATUS_SUCCESS);
        for (unsigned i = 0; i < 518; ++i)
            assert(X_M8(0x2dfb + i) == ((i == 517 && test < 2) ? 1 : disk.data[i]));
        assert(disk.data[517] == (test == 6));
    }
    /* Include a large value, unaligned info, and noncontiguous guest pages. */
    check_reservation(0x1103, 0x380000, 20, 0);
    check_reservation(0x1103, UINT64_C(0x123456789abcdef0), 19, 0);
    check_reservation(0x2ffc, UINT64_C(0x123456789abcdef0), 20, 0);
    check_reservation(0x1103, 0x380000, 20, 1);
    free(g_xpt); free(g_xram);
    puts("PASS: fragmented/contiguous file I/O, guards, short I/O, errors, EOF, IOSB, completion and 64-bit file reservations");
}
