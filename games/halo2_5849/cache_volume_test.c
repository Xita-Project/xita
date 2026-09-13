/* Synthetic FATX layouts and guest ABI tests. No game bytes or addresses. */
#include <assert.h>
#include "cache_volume.c"
uint8_t *g_xram;
uint32_t *g_xpt;
struct xk_file { unsigned char bytes[8 * 1024 * 1024]; int64_t size; int short_read; } disk;
struct xk_dir { int unused; } directory;
static xk_obj object;
static int populated, fallback, directory_cursor;
static const char *expected_open_name;
int64_t xk_os_size(xk_file *f) { return f->size; }
int64_t xk_os_read(xk_file *f, uint64_t pos, void *out, uint32_t n)
{
    if (f->short_read) return 0;
    assert(pos + n <= sizeof f->bytes); memcpy(out, f->bytes + pos, n); return n;
}
xk_file *xk_os_open(const char *p, int w, int create, int trunc, int *missing)
{ (void)p; (void)w; (void)create; (void)trunc; (void)missing; return &disk; }
void xk_os_close(xk_file *f) { (void)f; }
int xk_os_freespace(const char *p, uint64_t *free_bytes, uint64_t *total)
{ (void)p; *free_bytes = *total = 4ull << 30; return 0; }
xk_dir *xk_os_opendir(const char *p) { (void)p; directory_cursor = 0; return &directory; }
int xk_os_readdir(xk_dir *d, char *n, unsigned cap, int *is_dir, uint64_t *size)
{ (void)d; (void)cap; if (populated && !directory_cursor++) { strcpy(n, "existing.bin"); *is_dir = 0; *size = 65537; return 1; } return 0; }
void xk_os_closedir(xk_dir *d) { (void)d; }
xk_obj *xk_handle_get(uint32_t h) { return h == 7 ? &object : NULL; }
void xk_os_log(const char *fmt, ...) { (void)fmt; }
void __real_xk_NtDeviceIoControlFile(xctx *c) { ++fallback; finish(c, X_ARG(4), STATUS_INVALID_DEVICE_REQUEST, 0, 10); }
void __real_xk_NtReadFile(xctx *c) { ++fallback; finish(c, X_ARG(4), STATUS_SUCCESS, X_ARG(6), 8); }
void __real_xk_NtWriteFile(xctx *c) { ++fallback; finish(c, X_ARG(4), STATUS_SUCCESS, X_ARG(6), 8); }
void __real_xk_NtQueryVolumeInformationFile(xctx *c) { ++fallback; finish(c, X_ARG(1), STATUS_INVALID_INFO_CLASS, 0, 5); }
uint32_t xk_kalloc(uint32_t size) { assert(size <= 128); return 0x800; }
void xk_ansi_to_c(uint32_t a, char *out, unsigned cap)
{
    unsigned length = AS_LEN(a); if (length >= cap) length = cap - 1;
    for (unsigned i = 0; i < length; ++i) out[i] = X_M8(AS_BUF(a) + i);
    out[length] = 0;
}
void __real_xk_NtOpenFile(xctx *c)
{
    char name[128]; xk_ansi_to_c(OA_NAME(X_ARG(2)), name, sizeof name);
    assert(expected_open_name && !strcmp(name, expected_open_name));
    X_M32(X_ARG(0)) = 7; ++fallback; finish(c, X_ARG(3), STATUS_SUCCESS, 1, 6);
}
static void put32(unsigned offset, uint32_t value)
{ for (unsigned i = 0; i < 4; ++i) disk.bytes[offset + i] = value >> (i * 8); }
static unsigned make_empty(unsigned sectors)
{
    memset(&disk, 0, sizeof disk); disk.size = H2_CACHE_CAPACITY;
    put32(0, 0x58544146); put32(4, 0x13572468); put32(8, sectors); put32(12, 1);
    unsigned clusters = H2_CACHE_CAPACITY / (sectors * 512);
    unsigned width = clusters + 1 < 65520 ? 2 : 4;
    unsigned fat = ((clusters + 1) * width + 4095) / 4096 * 4096;
    memset(disk.bytes + 4096, 0xff, width * 2); disk.bytes[4096] = 0xf8;
    unsigned root = 4096 + fat;
    memset(disk.bytes + root, 0xff, sectors * 512);
    return root;
}
static xctx call(uint32_t code, uint32_t length, uint32_t output)
{
    xctx c = {0}; c.r[4] = 0x100;
    for (unsigned i = 0; i < 11; ++i) X_M32(0x100 + i * 4) = 0;
    X_M32(0x104) = 7; X_M32(0x114) = 0x300; X_M32(0x118) = code;
    X_M32(0x124) = output; X_M32(0x128) = length;
    X_M32(0x300) = X_M32(0x304) = 0xdddddddd;
    return c;
}
int main(void)
{
    g_xram = calloc(1, 16384); g_xpt = calloc(1u << 20, 4); assert(g_xram && g_xpt);
    g_xpt[1] = 8192; g_xpt[2] = 4096; /* Separate host pages exercise output translation. */
    strcpy(backing[2], "test.raw"); strcpy(directories[2], "test-dir");
    object.type = XO_FILE; object.u.file.path = backing[2]; object.u.file.f = &disk;
    for (unsigned sectors = 1; sectors <= 128; sectors *= 2) {
        unsigned root = make_empty(sectors); assert(h2_cache_validate_empty(&disk));
        const unsigned bad[] = {0, 8, 12, 4096, 4104, root};
        for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) {
            disk.bytes[bad[i]] ^= 0x40; assert(!h2_cache_validate_empty(&disk)); disk.bytes[bad[i]] ^= 0x40;
        }
        disk.short_read = 1; assert(!h2_cache_validate_empty(&disk)); disk.short_read = 0;
        disk.size--; assert(!h2_cache_validate_empty(&disk)); disk.size++;
    }
    make_empty(32);
    for (unsigned length = 0; length <= 33; ++length) {
        for (unsigned which = 0; which < 2; ++which) {
            const uint32_t code = which ? 0x74004 : 0x70000, needed = which ? 32 : 24;
            memset(g_xram + 8192, 0xa5, 4096);
            xctx c = call(code, length, 0x1ff0); __wrap_xk_NtDeviceIoControlFile(&c);
            assert(c.r[4] == 0x12c && c.r[0] == (length < needed ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS));
            assert(IOSB_STATUS(0x300) == c.r[0] && IOSB_INFO(0x300) == (length < needed ? 0 : needed));
            if (length >= needed && !which) {
                assert(X_M64(0x1ff0) * X_M32(0x1ffc) * X_M32(0x2000) * X_M32(0x2004) == H2_CACHE_CAPACITY);
                assert(X_M32(0x1ff8) == 12 && X_M32(0x2004) == 512);
            }
            if (length >= needed && which) { assert(X_M64(0x1ff8) == H2_CACHE_CAPACITY && X_M32(0x2004) == 5); }
        }
    }
    xctx c = call(0x70000, 24, 0); __wrap_xk_NtDeviceIoControlFile(&c); assert(c.r[0] == STATUS_BUFFER_TOO_SMALL);
    c = call(0x12345678, 32, 0x1000); __wrap_xk_NtDeviceIoControlFile(&c); assert(c.r[0] == STATUS_INVALID_DEVICE_REQUEST);
    c = call(0x70000, 24, 0x1000); X_M32(0x104) = 999; __wrap_xk_NtDeviceIoControlFile(&c); assert(fallback == 1);
    for (unsigned fail = 0; fail < 3; ++fail) {
        make_empty(32); mounted[2] = 0; populated = fail == 1;
        if (fail == 2) disk.bytes[4096] = 0;
        c = call(0x90020, 0, 0); __wrap_xk_NtFsControlFile(&c);
        assert(c.r[0] == (fail ? STATUS_NOT_IMPLEMENTED : STATUS_SUCCESS)); assert(mounted[2] == !fail);
    }
    populated = 0; mounted[2] = 0;
    c = call(0x90021, 0, 0); __wrap_xk_NtFsControlFile(&c); assert(c.r[0] == STATUS_INVALID_DEVICE_REQUEST);
    for (unsigned writing = 0; writing < 2; ++writing) {
        for (unsigned invalid = 0; invalid < 3; ++invalid) {
            c = call(0, 0, 0); X_M32(0x11c) = 512; X_M32(0x120) = 0x400;
            X_M64(0x400) = invalid == 1 ? H2_CACHE_CAPACITY - 511 : H2_CACHE_CAPACITY - 512;
            mounted[2] = invalid == 2;
            if (writing) __wrap_xk_NtWriteFile(&c); else __wrap_xk_NtReadFile(&c);
            assert(c.r[0] == (invalid ? STATUS_INVALID_DEVICE_REQUEST : STATUS_SUCCESS));
            assert(c.r[4] == 0x124 && IOSB_INFO(0x300) == (invalid ? 0 : 512));
        }
    }
    object.type = XO_DIRECTORY; object.u.file.path = directories[2];
    for (unsigned sectors = 16; sectors <= 128; sectors *= 2) {
        make_empty(sectors);
        for (unsigned nonempty = 0; nonempty < 2; ++nonempty) {
            populated = nonempty;
            c = call(0, 0, 0); X_M32(0x108) = 0x300; X_M32(0x10c) = 0x1ff0; X_M32(0x110) = 24; X_M32(0x114) = 3;
            __wrap_xk_NtQueryVolumeInformationFile(&c);
            assert(c.r[0] == STATUS_SUCCESS && c.r[4] == 0x118 && IOSB_INFO(0x300) == 24);
            assert(X_M32(0x2000) == sectors && X_M32(0x2004) == 512);
            uint64_t used = 1 + (nonempty ? (65537 + sectors * 512 - 1) / (sectors * 512) : 0);
            assert(X_M64(0x1ff0) - X_M64(0x1ff8) == used);
        }
    }
    for (unsigned test = 0; test < 4; ++test) {
        const char *name = test == 3 ? "\\Device\\Harddisk0\\Partition5\\" : "\\Device\\Harddisk0\\Partition5";
        unsigned before = fallback;
        c = call(0, 0, 0); X_M32(0x104) = 0x500; X_M32(0x108) = 0x100003;
        X_M32(0x10c) = 0x600; X_M32(0x110) = 0x300; X_M32(0x114) = 0; X_M32(0x118) = test == 3 ? 1 : 0x18;
        OA_ROOT(0x600) = 0; OA_NAME(0x600) = 0x620; OA_ATTR(0x600) = 0x40;
        AS_LEN(0x620) = strlen(name); AS_MAX(0x620) = strlen(name) + 1; AS_BUF(0x620) = 0x640;
        for (unsigned i = 0; i <= strlen(name); ++i) X_M8(0x640 + i) = name[i];
        populated = test == 1; mounted[2] = test == 2;
        expected_open_name = test == 3 ? name : "\\Device\\H2Raw5";
        __wrap_xk_NtOpenFile(&c);
        assert(c.r[0] == ((test == 1 || test == 2) ? STATUS_NOT_IMPLEMENTED : STATUS_SUCCESS));
        assert(c.r[4] == 0x11c && X_M32(0x10c) == 0x600);
        assert(fallback - before == (test == 0 || test == 3));
    }
    free(g_xram); free(g_xpt);
    puts("Halo 2 cache volume: FAT16/FAT32 format, geometry/partition ABI, bounds and rejection tests passed");
}
