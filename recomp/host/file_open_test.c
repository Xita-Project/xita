/* NT creation over a deliberately permissive host: missing parents must never
 * reach the host open/mkdir operation (XAPI probes SaveMeta with OPEN_ALWAYS). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/xk.h"

uint8_t *g_xram;
uint32_t *g_xpt;
static xk_obj object;
static const char *guest_name;
static unsigned opens, mkdirs;
static int save_dir, metadata;
void xk_os_log(const char *fmt, ...) { (void)fmt; }
void xk_ansi_to_c(uint32_t a, char *out, unsigned cap) { (void)a; snprintf(out, cap, "%s", guest_name); }
xk_obj *xk_handle_get(uint32_t h) { (void)h; return NULL; }
xk_obj *xk_obj_new(xk_objtype t) { memset(&object, 0, sizeof object); object.type = t; return &object; }
void xk_obj_deref(xk_obj *o) { free(o->u.file.path); }
uint32_t xk_handle_create(xk_obj *o) { assert(o == &object); return 7; }
int xk_os_stat(const char *p, int *dir, uint64_t *size, uint64_t *time)
{
    int d;
    if (!strcmp(p, "/save")) d = 1;
    else if (!strcmp(p, "/save/profile") && save_dir) d = save_dir == 1;
    else if (!strcmp(p, "/save/profile/SaveMeta.xbx") && metadata) d = 0;
    else return -1;
    if (dir) *dir = d;
    if (size) *size = 0;
    if (time) *time = 0;
    return 0;
}
xk_dir *xk_os_opendir(const char *p) { (void)p; return NULL; }
int xk_os_readdir(xk_dir *d, char *n, unsigned cap, int *dir, uint64_t *size)
{ (void)d; (void)n; (void)cap; (void)dir; (void)size; return 0; }
void xk_os_closedir(xk_dir *d) { (void)d; }
xk_file *xk_os_open(const char *p, int write, int create, int truncate, int *missing)
{
    (void)write; (void)truncate; (void)missing;
    assert(!strcmp(p, "/save/profile/SaveMeta.xbx"));
    opens++;
    if (create) { save_dir = 1; metadata = 1; } /* permissive host bug */
    return (xk_file *)&object;
}
int xk_os_mkdir(const char *p) { assert(!strcmp(p, "/save/profile")); mkdirs++; save_dir = 1; return 0; }
extern void xk_NtCreateFile(xctx *);
static uint32_t create(const char *name, uint32_t disposition, int directory)
{
    guest_name = name;
    xctx c = {0}; c.r[4] = 0x100;
    X_M32(0x104) = 0x300; X_M32(0x108) = 0xc0000000;
    X_M32(0x10c) = 0x400; X_M32(0x110) = 0x308;
    X_M32(0x114) = 0; X_M32(0x118) = 0; X_M32(0x11c) = 0;
    X_M32(0x120) = disposition; X_M32(0x124) = directory ? 1 : 0x40;
    X_M32(0x400) = OB_DOS_DEVICES_DIRECTORY; X_M32(0x404) = 0x500;
    xk_NtCreateFile(&c);
    assert(c.r[4] == 0x128 && X_M32(0x308) == c.r[0]);
    if (c.r[0] == STATUS_SUCCESS) { assert(X_M32(0x300) == 7); free(object.u.file.path); }
    return c.r[0];
}
int main(void)
{
    g_xram = calloc(1, 4096); g_xpt = calloc(1u << 20, 4);
    xk_path_mount("\\device\\test", "/save"); xk_path_add_link("U:", "\\device\\test");
    assert(create("U:\\profile\\SaveMeta.xbx", 3, 0) == STATUS_OBJECT_PATH_NOT_FOUND);
    assert(!opens && !mkdirs && !save_dir && !metadata);
    save_dir = 2; /* a file cannot be a parent directory either */
    assert(create("U:\\profile\\SaveMeta.xbx", 3, 0) == STATUS_OBJECT_PATH_NOT_FOUND && !opens);
    save_dir = 0;
    assert(create("U:\\profile", 2, 1) == STATUS_SUCCESS && mkdirs == 1);
    assert(create("U:\\profile\\SaveMeta.xbx", 3, 0) == STATUS_SUCCESS && opens == 1 && metadata);
    assert(create("U:\\profile\\SaveMeta.xbx", 1, 0) == STATUS_SUCCESS && opens == 2);
    assert(create("U:\\profile\\SaveMeta.xbx", 2, 0) == STATUS_OBJECT_NAME_COLLISION && opens == 2);
    free(g_xram); free(g_xpt);
    puts("PASS: missing/non-directory parents, XAPI metadata probe, explicit directory creation and existing files");
}
