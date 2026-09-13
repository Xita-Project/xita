/* Real shared NT object/handle/file lifecycle over a private POSIX directory.
 * Synthetic guest inputs only; no game files or title addresses. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "kernel/xk.h"
extern void xk_NtOpenFile(xctx *), xk_NtCreateFile(xctx *), xk_NtClose(xctx *);
extern void xk_NtSetInformationFile(xctx *), xk_NtDuplicateObject(xctx *);
uint8_t *g_xram;
uint32_t *g_xpt;
static char root[] = "/tmp/xita-h2-file-lifetime-XXXXXX";
static xctx prepare(void)
{
    xctx c; memset(&c, 0xa5, sizeof c); c.r[4] = 0x1000;
    memset(X_G(0x1000), 0, 64); X_M32(0x1000) = 0x12345678;
    return c;
}
static uint32_t open_file(const char *name, unsigned disposition, unsigned options, int nt_open)
{
    char path[256]; snprintf(path, sizeof path, "\\Device\\Test\\%s", name);
    AS_LEN(0x240) = strlen(path); AS_MAX(0x240) = strlen(path) + 1; AS_BUF(0x240) = 0x260;
    strcpy(X_G(0x260), path); OA_ROOT(0x220) = 0; OA_NAME(0x220) = 0x240; OA_ATTR(0x220) = 0;
    X_M32(0x200) = 0xdeadbeef; X_M32(0x210) = X_M32(0x214) = 0xaaaaaaaa;
    xctx c = prepare(), before = c;
    X_M32(0x1004) = 0x200; X_M32(0x1008) = 0x40010000; X_M32(0x100c) = 0x220;
    X_M32(0x1010) = 0x210;
    if (nt_open) { X_M32(0x1014) = 7; X_M32(0x1018) = options; xk_NtOpenFile(&c); }
    else { X_M32(0x101c) = 7; X_M32(0x1020) = disposition; X_M32(0x1024) = options; xk_NtCreateFile(&c); }
    before.r[0] = c.r[0]; before.r[4] += nt_open ? 28 : 40;
    assert(!memcmp(&c, &before, sizeof c));
    assert(X_M32(0x210) == c.r[0]);
    if (c.r[0]) { assert(X_M32(0x200) == 0xdeadbeef); return c.r[0]; }
    return X_M32(0x200);
}
static void close_file(uint32_t h)
{
    xctx c = prepare(), before = c; X_M32(0x1004) = h; xk_NtClose(&c);
    before.r[0] = STATUS_SUCCESS; before.r[4] += 8;
    assert(!memcmp(&c, &before, sizeof c) && !xk_handle_get(h));
}
static void disposition(uint32_t h, int remove)
{
    xctx c = prepare(), before = c; X_M32(0x1004) = h; X_M32(0x1008) = 0x210;
    X_M32(0x100c) = 0x230; X_M32(0x1010) = 1; X_M32(0x1014) = 13;
    X_M8(0x230) = remove; xk_NtSetInformationFile(&c);
    before.r[0] = STATUS_SUCCESS; before.r[4] += 24;
    assert(!memcmp(&c, &before, sizeof c));
    assert(X_M32(0x210) == STATUS_SUCCESS && X_M32(0x214) == 0);
}
static int exists(const char *name)
{
    char path[512]; snprintf(path, sizeof path, "%s/%s", root, name);
    return access(path, F_OK) == 0;
}
int main(void)
{
    assert(mkdtemp(root));
    g_xram = calloc(1, 0x5000); g_xpt = calloc(1u << 20, 4); assert(g_xram && g_xpt);
    for (unsigned i = 0; i < 5; ++i) g_xpt[i] = i * 4096;
    xk_path_mount("\\Device\\Test", root);
    /* Legacy default is unchanged; explicitly release its leaked creator ref
     * inside this test so sanitizer cleanup remains meaningful. */
    uint32_t h = open_file("legacy", 2, 0, 0); xk_obj *o = xk_handle_get(h);
    assert(o && o->refs == 2); disposition(h, 1); close_file(h);
    assert(exists("legacy") && o->refs == 1); xk_obj_deref(o); assert(!exists("legacy"));
    xk_file_set_balanced_lifetime(1);
    h = open_file("cache", 2, 0, 0); o = xk_handle_get(h); assert(o && o->refs == 1);
    assert(xk_os_write(o->u.file.f, 0, "cache bytes", 11) == 11);
    xctx c = prepare(); X_M32(0x1004) = h; X_M32(0x1008) = 0x204;
    xk_NtDuplicateObject(&c); uint32_t duplicate = X_M32(0x204);
    assert(c.r[0] == 0 && o->refs == 2 && xk_handle_get(duplicate) == o);
    disposition(h, 1); close_file(h); assert(exists("cache") && o->refs == 1);
    char bytes[11]; assert(xk_os_read(o->u.file.f, 0, bytes, sizeof bytes) == 11 && !memcmp(bytes, "cache bytes", 11));
    close_file(duplicate); assert(!exists("cache"));
    h = open_file("cache", 2, 0x1000, 0); assert(exists("cache")); close_file(h); assert(!exists("cache"));
    h = open_file("retain", 2, 0, 0); disposition(h, 1); disposition(h, 0); close_file(h); assert(exists("retain"));
    h = open_file("retain", 1, 0, 1); assert(xk_handle_get(h)->refs == 1);
    disposition(h, 1); close_file(h); assert(!exists("retain"));
    h = open_file("directory", 2, 1, 0); o = xk_handle_get(h); assert(o->type == XO_DIRECTORY && o->refs == 1);
    o->u.file.dir = xk_os_opendir(o->u.file.path); assert(o->u.file.dir);
    disposition(h, 1); close_file(h); assert(!exists("directory"));
    assert(open_file("missing", 1, 0, 1) == STATUS_OBJECT_NAME_NOT_FOUND);
    /* Exhaust the real shared handle table: no fabricated success/zero handle,
     * and the failed open's descriptor/object are released (ASan leak check). */
    o = xk_obj_new(XO_EVENT); uint32_t handles[XK_MAX_HANDLES]; unsigned count = 0;
    while ((h = xk_handle_create(o)) != 0) handles[count++] = h;
    xk_obj_deref(o);
    assert(open_file("exhausted", 2, 0x1000, 0) == STATUS_TOO_MANY_OPENED_FILES);
    assert(X_M32(0x214) == 0 && !exists("exhausted"));
    for (unsigned i = 0; i < count; ++i) close_file(handles[i]);
    assert(rmdir(root) == 0); free(g_xpt); free(g_xram);
    puts("File lifecycle: default preserved; final close, duplication, deletion/recreation, directories and exhaustion pass.");
    return 0;
}
