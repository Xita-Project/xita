/* xk_file.c - NT file API over xk_os: Nt{Create,Open,Read,Write,Close}File, information classes, directories,
 * symbolic links (\??\D: etc.) and Xbox path translation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "xk.h"
#include "xk_quality.h"
/* The map transformation belongs to an optional game adapter. */
void xk_quality_map_read(uint32_t address, uint32_t bytes) __attribute__((weak));
static int ce_adapter_enabled = 1;
void xk_file_set_ce_adapter_enabled(int enabled) { ce_adapter_enabled = enabled != 0; }

/* ---- namespace: devices -> host dirs, links -> devices ------------------------------------ */
typedef struct { char name[64]; char target[256]; } link_t;
static link_t g_mounts[16]; static int g_nmounts;
static link_t g_links[32];  static int g_nlinks;

static void lower(char *s) { for (; *s; ++s) *s = (char)tolower((unsigned char)*s); }

void xk_path_mount(const char *device, const char *host_dir)
{
    if (g_nmounts >= 16) return;
    snprintf(g_mounts[g_nmounts].name, 64, "%s", device); lower(g_mounts[g_nmounts].name);
    snprintf(g_mounts[g_nmounts].target, 256, "%s", host_dir); g_nmounts++;
}
void xk_path_add_link(const char *name, const char *target)
{
    char n[64]; snprintf(n, sizeof n, "%s", name); lower(n);
    for (int i = 0; i < g_nlinks; ++i) if (!strcmp(g_links[i].name, n)) { snprintf(g_links[i].target, 256, "%s", target); return; }
    if (g_nlinks >= 32) return;
    strcpy(g_links[g_nlinks].name, n); snprintf(g_links[g_nlinks].target, 256, "%s", target); g_nlinks++;
}
static const char *link_lookup(const char *name)
{
    char n[64]; snprintf(n, sizeof n, "%s", name); lower(n);
    for (int i = 0; i < g_nlinks; ++i) if (!strcmp(g_links[i].name, n)) return g_links[i].target;
    return NULL;
}

/* "\??\D:\maps\ui.map", "D:\maps\ui.map" (with DosDevices root), "\Device\Cdrom0\maps\ui.map" -> host path */
/* The D: drive is the game's own data (haloce/) and is treated as read-only media: the game never needs to
 * write there, and a bug once let a cache rebuild overwrite a map file in place.  Writes are refused. */
static int host_path_is_media(const char *host) { return host && strstr(host, "/haloce/") != NULL; }
char *xk_path_translate_raw(const char *xbox_path, uint32_t root_handle);
char *xk_path_translate(const char *xbox_path, uint32_t root_handle) { return xk_path_translate_raw(xbox_path, root_handle); }
char *xk_path_translate_raw(const char *xbox_path, uint32_t root_handle)
{
    char full[1024];
    if (root_handle == OB_DOS_DEVICES_DIRECTORY) snprintf(full, sizeof full, "\\??\\%s", xbox_path);
    else if (root_handle && root_handle != OB_WIN32_NAMED_OBJECTS) {
        xk_obj *o = xk_handle_get(root_handle);
        if (o && (o->type == XO_FILE || o->type == XO_DIRECTORY) && o->u.file.path) {
            char *r = malloc(strlen(o->u.file.path) + strlen(xbox_path) + 2);
            sprintf(r, "%s/%s", o->u.file.path, xbox_path);
            for (char *p = r; *p; ++p) if (*p == '\\') *p = '/';
            return r;
        }
        return NULL;
    } else snprintf(full, sizeof full, "%s", xbox_path);
    /* resolve \??\X: link prefix (iteratively) */
    for (int iter = 0; iter < 4; ++iter) {
        if (strncasecmp(full, "\\??\\", 4) == 0) {
            char *rest = full + 4; char *sep = strchr(rest, '\\'); size_t nl = sep ? (size_t)(sep - rest) : strlen(rest);
            char name[64]; snprintf(name, sizeof name, "%.*s", (int)nl, rest);
            const char *t = link_lookup(name);
            if (!t) { XK_LOG("path: no link for \\??\\%s\n", name); return NULL; }
            char tmp[1024]; snprintf(tmp, sizeof tmp, "%s%s", t, sep ? sep : ""); strcpy(full, tmp);
            continue;
        }
        break;
    }
    /* device prefix */
    char lowered[1024]; snprintf(lowered, sizeof lowered, "%s", full); lower(lowered);
    for (int i = 0; i < g_nmounts; ++i) {
        size_t n = strlen(g_mounts[i].name);
        if (strncmp(lowered, g_mounts[i].name, n) == 0 && (full[n] == 0 || full[n] == '\\')) {
            char *r = malloc(strlen(g_mounts[i].target) + strlen(full) - n + 2);
            sprintf(r, "%s%s", g_mounts[i].target, full + n);
            for (char *p = r + strlen(g_mounts[i].target); *p; ++p) if (*p == '\\') *p = '/';
            size_t L = strlen(r); if (L > 1 && r[L - 1] == '/') r[L - 1] = 0;
            return r;
        }
    }
    XK_LOG("path: unmapped device in \"%s\"\n", full);
    return NULL;
}

/* case-insensitive fix-up: Xbox FATX is case-insensitive, Linux is not.  Try exact, then per component. */
static int stat_ci(char *path, int *is_dir, uint64_t *size, uint64_t *mtime)
{
    if (xk_os_stat(path, is_dir, size, mtime) == 0) return 0;
    char fixed[1024] = ""; char *save = NULL, *tmp = strdup(path);
    int abs = tmp[0] == '/';
    for (char *comp = strtok_r(tmp, "/", &save); comp; comp = strtok_r(NULL, "/", &save)) {
        char probe[1024]; snprintf(probe, sizeof probe, "%s%s%s", fixed, (abs || fixed[0]) ? "/" : "", comp);
        if (xk_os_stat(probe, NULL, NULL, NULL) != 0) {
            xk_dir *d = xk_os_opendir(fixed[0] ? fixed : (abs ? "/" : "."));
            int found = 0;
            if (d) { char name[512]; int isd; uint64_t sz; while (xk_os_readdir(d, name, sizeof name, &isd, &sz)) if (!strcasecmp(name, comp)) { snprintf(probe, sizeof probe, "%s%s%s", fixed, (abs || fixed[0]) ? "/" : "", name); found = 1; break; } xk_os_closedir(d); }
            if (!found) { free(tmp); return -1; }
        }
        strcpy(fixed, probe);
    }
    free(tmp);
    strcpy(path, fixed);
    return xk_os_stat(path, is_dir, size, mtime);
}

/* ---- file objects ---------------------------------------------------------------------- */
static xk_obj *file_from_handle(uint32_t h) { xk_obj *o = xk_handle_get(h); return o && (o->type == XO_FILE || o->type == XO_DIRECTORY) ? o : NULL; }

int xk_file_in_ui_map = 1;                 /* last cache map opened was ui.map (main menu) */
static uint32_t open_common(xctx *c, uint32_t phandle, uint32_t access, uint32_t oa, uint32_t iosb, uint32_t disposition, uint32_t options, int is_create)
{
    char name[512]; xk_ansi_to_c(OA_NAME(oa), name, sizeof name);
    char *host = xk_path_translate(name, OA_ROOT(oa));
    if (!host) { if (iosb) IOSB_STATUS(iosb) = STATUS_OBJECT_PATH_NOT_FOUND; return STATUS_OBJECT_PATH_NOT_FOUND; }
    int is_dir = 0; uint64_t size = 0;
    int exists = stat_ci(host, &is_dir, &size, NULL) == 0;
    int want_dir = (options & 0x0001) != 0;                 /* FILE_DIRECTORY_FILE */
    int write = (access & 0x40000006u) != 0 || disposition >= 2;   /* GENERIC_WRITE|FILE_WRITE_DATA|APPEND or create/overwrite */
    uint32_t status = STATUS_SUCCESS, info = 1;             /* FILE_OPENED */
    /* FILE_SUPERSEDE 0, OPEN 1, CREATE 2, OPEN_IF 3, OVERWRITE 4, OVERWRITE_IF 5 */
    if (!exists) {
        /* FILE_OVERWRITE (4) on a missing file creates it: Halo writes z:\lastmpvr.txt that way and
         * shows "Unable to load saved game file" when the open fails - FATX is lenient here */
        if (disposition == 1) { XK_LOG("%s \"%s\" -> %s: not found (disp %u)\n", is_create ? "NtCreateFile" : "NtOpenFile", name, host, disposition); free(host); if (iosb) IOSB_STATUS(iosb) = STATUS_OBJECT_NAME_NOT_FOUND; return STATUS_OBJECT_NAME_NOT_FOUND; }
        /* Creating a file never creates its parent directories. Vita3K's host
         * open may do that implicitly: XAPI's OPEN_ALWAYS SaveMeta probe then
         * leaves an empty save directory and subsequent profile creation fails. */
        char parent[1024]; snprintf(parent, sizeof parent, "%s", host);
        char *slash = strrchr(parent, '/');
        if (slash && slash != parent) {
            int parent_dir = 0; *slash = 0;
            if (stat_ci(parent, &parent_dir, NULL, NULL) != 0 || !parent_dir) {
                free(host);
                if (iosb) { IOSB_STATUS(iosb) = STATUS_OBJECT_PATH_NOT_FOUND; IOSB_INFO(iosb) = 0; }
                return STATUS_OBJECT_PATH_NOT_FOUND;
            }
        }
        if (want_dir) { if (xk_os_mkdir(host) != 0) { free(host); return STATUS_OBJECT_PATH_NOT_FOUND; } is_dir = 1; }
        info = 2;                                            /* FILE_CREATED */
    } else if (disposition == 2) { free(host); if (iosb) IOSB_STATUS(iosb) = STATUS_OBJECT_NAME_COLLISION; return STATUS_OBJECT_NAME_COLLISION; }
    else if (disposition == 0 || disposition == 4 || disposition == 5) info = 3;   /* FILE_OVERWRITTEN */
    if (exists && is_dir && (options & 0x0040)) { free(host); return STATUS_FILE_IS_A_DIRECTORY; }   /* FILE_NON_DIRECTORY_FILE */
    if (exists && !is_dir && want_dir) { free(host); return STATUS_NOT_A_DIRECTORY; }
    xk_obj *o = xk_obj_new(is_dir ? XO_DIRECTORY : XO_FILE);
    o->u.file.path = host; o->u.file.is_dir = is_dir; o->u.file.map_type = -1;
    o->u.file.delete_on_close = (options & 0x1000) != 0;
    o->u.file.append = (access & 0x4) && !(access & 0x2);
    if (!is_dir) {
        int missing = 0;
        int media = host_path_is_media(host);
        if (media && (write || info == 3)) { XK_LOG("NtCreateFile: refusing write access to game media %s\n", host); write = 0; }
        o->u.file.f = xk_os_open(host, write, !media && (!exists || disposition == 0 || disposition == 2 || disposition == 3 || disposition == 5), !media && info == 3, &missing);
        if (!o->u.file.f && write) o->u.file.f = xk_os_open(host, 0, 0, 0, &missing);   /* read-only media */
        if (!o->u.file.f) { status = missing ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_ACCESS_DENIED; xk_obj_deref(o); if (iosb) IOSB_STATUS(iosb) = status; return status; }
    }
    uint32_t h = xk_handle_create(o);
    X_M32(phandle) = h;
    if (iosb) { IOSB_STATUS(iosb) = status; IOSB_INFO(iosb) = info; }
    XK_LOG("%s \"%s\" -> %s (h=%X)%s\n", is_create ? "NtCreateFile" : "NtOpenFile", name, host, h, is_dir ? " [dir]" : "");
    return status;
}

/* NTSTATUS NtCreateFile(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions) */
void xk_NtCreateFile(xctx *c) { c->r[0] = open_common(c, X_ARG(0), X_ARG(1), X_ARG(2), X_ARG(3), X_ARG(7), X_ARG(8), 1); X_RET(9); }
/* NTSTATUS NtOpenFile(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG ShareAccess, ULONG OpenOptions) */
void xk_NtOpenFile(xctx *c) { c->r[0] = open_common(c, X_ARG(0), X_ARG(1), X_ARG(2), X_ARG(3), 1, X_ARG(5), 0); X_RET(6); }
void xk_IoCreateFile(xctx *c) { c->r[0] = open_common(c, X_ARG(0), X_ARG(1), X_ARG(2), X_ARG(3), X_ARG(7), X_ARG(8), 1); X_RET(10); }

void xk_NtClose(xctx *c)
{
    uint32_t h = X_ARG(0);
    xk_obj *o = xk_handle_get(h);
    if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(1); }
    xk_handle_close(h);
    c->r[0] = STATUS_SUCCESS; X_RET(1);
}

/* A guest buffer can span separately committed/reused pages. Keep adjacent host
 * pages in one I/O request (map streaming is normally contiguous), but translate
 * again at every discontinuity instead of reading/writing unrelated arena data. */
static int64_t file_guest_io(xk_file *f, uint64_t pos, uint32_t buf, uint32_t len, int writing)
{
    uint32_t done = 0;
    do {
        uint32_t a = buf + done, left = len - done;
        uint32_t n = 4096u - (a & 0xFFFu);
        if (n > left) n = left;
        while (n < left && (uint64_t)a + n <= UINT32_MAX &&
               g_xpt[(a + n) >> 12] == g_xpt[a >> 12] + (a & 0xFFFu) + n) {
            uint32_t more = left - n;
            n += more < 4096u ? more : 4096u;
        }
        int64_t got = writing ? xk_os_write(f, pos + done, X_G(a), n)
                              : xk_os_read(f, pos + done, X_G(a), n);
        if (got < 0) return done ? (int64_t)done : got;
        done += (uint32_t)got;
        if ((uint64_t)got < n) break; /* EOF or short I/O: leave the remainder untouched */
    } while (done < len);
    return done;
}

static int builtin_profile_path(const char *path, int guest)
{
    const char *names[2] = {"00.sav", "01.sav"};
    const char *prefix = guest ? "z:\\saved\\player_profiles\\default_profile\\"
                               : "/cache/saved/player_profiles/default_profile/";
    size_t n = strlen(path), prefix_n = strlen(prefix), suffix_n = prefix_n + 6;
    if (n < suffix_n || (guest && n != suffix_n) ||
        memcmp(path + n - suffix_n, prefix, prefix_n)) return -1;
    for (int i = 0; i < 2; ++i)
        if (!strcmp(path + n - 6, names[i])) return i;
    return -1;
}

/* NTSTATUS NtReadFile(HANDLE, HANDLE Event, PIO_APC_ROUTINE, PVOID ApcContext, PIO_STATUS_BLOCK, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset) */
void xk_NtReadFile(xctx *c)
{
    xk_obj *o = file_from_handle(X_ARG(0)); uint32_t iosb = X_ARG(4), buf = X_ARG(5), len = X_ARG(6), poff = X_ARG(7);
    if (!o || o->type != XO_FILE) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(8); }
    uint64_t pos = poff ? LI64(poff) : o->u.file.pos;
    if (poff && (LI64(poff) == 0xFFFFFFFFFFFFFFFEull)) pos = o->u.file.pos;   /* FILE_USE_FILE_POINTER_POSITION */
    uint32_t apc_before = X_ARG(2), ctx_before = X_ARG(3);
    int64_t got = file_guest_io(o->u.file.f, pos, buf, len, 0);
    if (ce_adapter_enabled && got > 0 && (uint64_t)got == len && buf == 0x803A6000u && o->u.file.path) {
        size_t length = strlen(o->u.file.path);
        if (xk_quality_map_read && length > 4 && !strcmp(o->u.file.path+length-4,".map"))
            xk_quality_map_read(buf,(uint32_t)got);
    }
    if (X_ARG(2) != apc_before || X_ARG(3) != ctx_before)
        XK_LOG("READ CLOBBERED THE STACK: esp %08X (page->arena %08X), buf %08X len %u (buf arena %08X..); apc %08X->%08X\n",
               c->r[4], g_xpt[c->r[4] >> 12], buf, len, g_xpt[buf >> 12], apc_before, X_ARG(2));
    uint32_t st = got < 0 ? STATUS_UNSUCCESSFUL : got == 0 && len ? STATUS_END_OF_FILE : STATUS_SUCCESS;
    if (ce_adapter_enabled && pos == 0 && got == 512 && len == 512) {
        const char *pth = o->u.file.path; size_t hl = strlen(pth);
        if (hl >= 9 && !strcmp(pth + hl - 9, "/blam.lst") && xk_variant_recover_unsigned(buf))
            XK_LOG("[variant] recovered legacy unsigned settings: %s\n", pth);
        int preset = builtin_profile_path(pth, 0);
        if (preset >= 0 && xk_builtin_profile_recover(buf, (unsigned)preset))
            XK_LOG("[profile] recovered built-in preset signature: %s\n", pth);
    }
    { static const char *watch = NULL; static int winit; if (!winit) { winit = 1; watch = getenv("XV_LOG_READS"); }
      if (watch && strstr(o->u.file.path, watch)) { XK_LOG("NtReadFile(%s @%llu, %u B) = %lld st %08X buf %08X\n", o->u.file.path, (unsigned long long)pos, len, (long long)got, st, buf);
          static unsigned ns; if (len > 4096 && ns++ < 12) { char sb[400]; int k = 0; uint32_t esp = c->r[4]; for (unsigned j = 0; j < 128 && k < 380; ++j) { uint32_t w = X_M32(esp + 4 * j); if (w >= 0x11000 && w < 0x3A0000) k += snprintf(sb + k, sizeof sb - k, " %X", w); } XK_LOG("  read stack:%s\n", sb); } } }
    { static unsigned n; if (n++ < 40) XK_LOG("NtReadFile(%s @%llu, %u B -> %08X) = %lld (ev %08X apc %08X ctx %08X)\n", o->u.file.path, (unsigned long long)pos, len, buf, (long long)got, X_ARG(1), X_ARG(2), X_ARG(3)); }
    {   /* XV_SLOW_READ=<us per 64 KB>: throttle .map reads to mimic the memory card (loading-screen work in Vita3K) */
        static int slow = -1; if (slow < 0) { const char *e = getenv("XV_SLOW_READ"); slow = e ? atoi(e) : 0; }
        if (slow > 0 && got >= 4096) { const char *pth = o->u.file.path; size_t hl = strlen(pth); if (hl > 4 && !strcmp(pth + hl - 4, ".map")) xk_os_sleep_us((uint64_t)slow * ((uint64_t)got / 65536 + 1)); }
    }
    {   /* Saved-file index records (z:\saved\hdmu.map, 518 bytes: path[256] name[256] u16 type u16 index
         * u8 is_default u8 valid): the game's gametype-select handler (3925: 0xCD9C0) only starts a game
         * when the variant's handle carries bit 31 = record byte 517 ("valid").  The default variants in
         * our saves carry 0 there (index rows created by an early runtime), so present them as valid on
         * read - the playlist files themselves are intact and load fine. */
        const char *pth = o->u.file.path; size_t hl = strlen(pth);
        if (ce_adapter_enabled && got == 518 && len == 518 && hl >= 8 && !strcmp(pth + hl - 8, "hdmu.map")) {
            uint8_t flags[6]; x_guest_read(flags, buf + 512, sizeof flags);
            int variant = flags[0] == 1 && flags[1] == 0;
            int profile = 0;
            if (flags[0] == 0 && flags[1] == 0 && flags[4] == 1) {
                char path[257]; x_guest_read(path, buf, 256); path[256] = 0;
                profile = builtin_profile_path(path, 1) >= 0;
            }
            if ((variant || profile) && flags[5] == 0) {
                uint8_t valid = 1; x_guest_write(buf + 517, &valid, 1);
                static unsigned n; if (n++ < 6) XK_LOG("hdmu.map record @%llu: %s marked valid\n",
                    (unsigned long long)pos, profile ? "built-in profile" : "variant");
            }
        }
    }
    if (got > 0) o->u.file.pos = pos + (uint64_t)got;
    { static int sl = -1; if (sl < 0) { const char *e = getenv("XV_SAVE_LOG"); sl = e ? atoi(e) : 0; }
      if (sl && o->u.file.path) { const char *b = o->u.file.path; size_t hl = strlen(b);
          if ((hl >= 12 && !strcmp(b + hl - 12, "savegame.bin")) || (hl >= 8 && !strcmp(b + hl - 8, "blam.sav")) || (hl >= 12 && !strcmp(b + hl - 12, "SaveMeta.xbx")))
              XK_LOG("[save] READ %s @%llu len %u -> %lld\n", b, (unsigned long long)pos, len, (long long)got); } }
    if (got > 0) { extern void xv_ui_gxm_invalidate_range(uint32_t, uint32_t) __attribute__((weak)); if (xv_ui_gxm_invalidate_range) xv_ui_gxm_invalidate_range(buf, (uint32_t)got); }   /* streamed bitmap slots reuse memory: re-check overlapping cached textures */
    if (got > 0x100000) {                                  /* any multi-MB read: map tag data (0x803A6000) or a BSP switch - both reuse texture memory wholesale */ extern void xv_ui_gxm_request_texture_purge(void) __attribute__((weak)); if (xv_ui_gxm_request_texture_purge) xv_ui_gxm_request_texture_purge(); XK_LOG("map tag data loaded: texture cache purge requested\n"); }
    {   /* Which kind of cache map is being STREAMED: Halo copies maps to z:\cacheNNN.map, so only the
         * header tells (+0x60: 0 campaign, 1 multiplayer, 2 ui).  Level-select screens peek at headers only;
         * a read past the 2 KB header means the map is loading/loaded.  The pad layer keeps the D-pad plain
         * while a UI map is current. */
        const char *pth = o->u.file.path; size_t hl = strlen(pth);
        if (ce_adapter_enabled && got > 0 && hl > 4 && !strcmp(pth + hl - 4, ".map")) {
            if (pos == 0 && got >= 0x64 && X_M32(buf) == 0x68656164u) o->u.file.map_type = (int)X_M32(buf + 0x60);   /* 'head' tag: bytes d a e h */
            if (pos >= 2048 && o->u.file.map_type >= 0) xk_file_in_ui_map = (o->u.file.map_type == 2);
        }
    }
    if (iosb) { IOSB_STATUS(iosb) = st; IOSB_INFO(iosb) = got > 0 ? (uint32_t)got : 0; }
    if (X_ARG(1)) { xk_obj *ev = xk_handle_get_type(X_ARG(1), XO_EVENT); if (ev) { ev->u.event.signaled = 1; xk_signal_check(); } }
    if (X_ARG(2)) xk_apc_queue(xk_cur, X_ARG(2), X_ARG(3), iosb, 0);        /* completion APC */
    c->r[0] = st; X_RET(8);
}
void xk_NtWriteFile(xctx *c)
{
    xk_obj *o = file_from_handle(X_ARG(0)); uint32_t iosb = X_ARG(4), buf = X_ARG(5), len = X_ARG(6), poff = X_ARG(7);
    if (!o || o->type != XO_FILE) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(8); }
    uint64_t pos = poff && LI64(poff) != 0xFFFFFFFFFFFFFFFEull ? LI64(poff) : o->u.file.pos;
    if (o->u.file.append) { int64_t s = xk_os_size(o->u.file.f); pos = s > 0 ? (uint64_t)s : 0; }
    int64_t got = file_guest_io(o->u.file.f, pos, buf, len, 1);
    { static const char *watch; static int init; static unsigned traces;
      if (!init) { init = 1; watch = getenv("XV_LOG_WRITES"); }
      if (watch && o->u.file.path && strstr(o->u.file.path, watch) && traces++ < 48) {
          XK_LOG("[write-trace] %s @%llu len %u buf %08X -> %lld first %08X\n",
              o->u.file.path, (unsigned long long)pos, len, buf, (long long)got,
              len >= 4 ? X_M32(buf) : 0);
          char sb[400] = {0}; int k = 0;
          for (unsigned j = 0; j < 128 && k < 380; j++) {
              uint32_t w = X_M32(c->r[4] + 4u * j);
              if (w >= 0x11000 && w < 0x3A0000) k += snprintf(sb + k, sizeof sb - k, " %X", w);
          }
          XK_LOG("[write-trace] stack:%s\n", sb);
      } }
    { static unsigned n; if (n++ < 12) XK_LOG("NtWriteFile(%s @%llu, %u B) = %lld\n", o->u.file.path, (unsigned long long)pos, len, (long long)got); }
    { static int sl = -1; if (sl < 0) { const char *e = getenv("XV_SAVE_LOG"); sl = e ? atoi(e) : 0; }
      if (sl && o->u.file.path) { const char *b = o->u.file.path; size_t hl = strlen(b);
          if ((hl >= 12 && !strcmp(b + hl - 12, "savegame.bin")) || (hl >= 8 && !strcmp(b + hl - 8, "blam.sav")) || (hl >= 12 && !strcmp(b + hl - 12, "SaveMeta.xbx")))
              XK_LOG("[save] WRITE %s @%llu len %u -> %lld\n", b, (unsigned long long)pos, len, (long long)got); } }
    uint32_t st = got < 0 ? STATUS_ACCESS_DENIED : STATUS_SUCCESS;
    if (got > 0) o->u.file.pos = pos + (uint64_t)got;
    if (iosb) { IOSB_STATUS(iosb) = st; IOSB_INFO(iosb) = got > 0 ? (uint32_t)got : 0; }
    if (X_ARG(1)) { xk_obj *ev = xk_handle_get_type(X_ARG(1), XO_EVENT); if (ev) { ev->u.event.signaled = 1; xk_signal_check(); } }
    if (X_ARG(2)) xk_apc_queue(xk_cur, X_ARG(2), X_ARG(3), iosb, 0);
    c->r[0] = st; X_RET(8);
}
void xk_NtFlushBuffersFile(xctx *c) { xk_obj *o = file_from_handle(X_ARG(0)); if (o && o->u.file.f) xk_os_flush(o->u.file.f); if (X_ARG(1)) IOSB_STATUS(X_ARG(1)) = 0; c->r[0] = STATUS_SUCCESS; X_RET(2); }

/* information classes */
enum { FileDirectoryInformation = 1, FileBasicInformation = 4, FileStandardInformation = 5, FileInternalInformation = 6, FileEaInformation = 7,
       FileAccessInformation = 8, FileNameInformation = 9, FileRenameInformation = 10, FileLinkInformation = 11, FileNamesInformation = 12,
       FileDispositionInformation = 13, FilePositionInformation = 14, FileFullEaInformation = 15, FileModeInformation = 16,
       FileAlignmentInformation = 17, FileAllInformation = 18, FileAllocationInformation = 19, FileEndOfFileInformation = 20,
       FileAlternateNameInformation = 21, FileStreamInformation = 22, FileNetworkOpenInformation = 34, FileAttributeTagInformation = 35 };

static void fill_basic(uint32_t p, uint64_t mtime, int is_dir) { LI64(p) = mtime; LI64(p + 8) = mtime; LI64(p + 16) = mtime; LI64(p + 24) = mtime; X_M32(p + 32) = is_dir ? 0x10 : 0x80; }
static void fill_standard(uint32_t p, uint64_t size, int is_dir) { LI64(p) = (size + 4095) & ~4095ull; LI64(p + 8) = size; X_M32(p + 16) = 1; X_M8(p + 20) = 0; X_M8(p + 21) = (uint8_t)is_dir; }

/* NTSTATUS NtQueryInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID Info, ULONG Length, FILE_INFORMATION_CLASS) */
void xk_NtQueryInformationFile(xctx *c)
{
    xk_obj *o = file_from_handle(X_ARG(0)); uint32_t iosb = X_ARG(1), info = X_ARG(2), len = X_ARG(3), cls = X_ARG(4);
    uint32_t st = STATUS_SUCCESS, written = 0;
    if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(5); }
    int is_dir = o->type == XO_DIRECTORY; uint64_t size = 0, mtime = 0;
    xk_os_stat(o->u.file.path, NULL, &size, &mtime);
    switch (cls) {
    case FileBasicInformation: if (len < 40) st = STATUS_INFO_LENGTH_MISMATCH; else { fill_basic(info, mtime, is_dir); written = 40; } break;
    case FileStandardInformation: if (len < 24) st = STATUS_INFO_LENGTH_MISMATCH; else { fill_standard(info, size, is_dir); written = 24; } break;
    case FileInternalInformation: LI64(info) = (uint64_t)(uintptr_t)o; written = 8; break;
    case FileEaInformation: X_M32(info) = 0; written = 4; break;
    case FileAccessInformation: X_M32(info) = 0x1F01FF; written = 4; break;
    case FileNameInformation: { const char *base = strrchr(o->u.file.path, '/'); base = base ? base + 1 : o->u.file.path; uint32_t n = (uint32_t)strlen(base);
        X_M32(info) = n; if (len < 4 + n) { st = STATUS_BUFFER_OVERFLOW; n = len > 4 ? len - 4 : 0; } memcpy(X_G(info + 4), base, n); written = 4 + n; break; }
    case FilePositionInformation: LI64(info) = o->u.file.pos; written = 8; break;
    case FileModeInformation: X_M32(info) = 0x20; written = 4; break;         /* FILE_SYNCHRONOUS_IO_NONALERT */
    case FileAlignmentInformation: X_M32(info) = 0; written = 4; break;
    case FileNetworkOpenInformation: if (len < 56) st = STATUS_INFO_LENGTH_MISMATCH; else { fill_basic(info, mtime, is_dir); LI64(info + 32) = (size + 4095) & ~4095ull; LI64(info + 40) = size; X_M32(info + 48) = is_dir ? 0x10 : 0x80; written = 56; } break;
    case FileAllInformation: if (len < 96) st = STATUS_INFO_LENGTH_MISMATCH; else { fill_basic(info, mtime, is_dir); fill_standard(info + 40, size, is_dir); LI64(info + 64) = 0; X_M32(info + 72) = 0; X_M32(info + 76) = 0x1F01FF; LI64(info + 80) = o->u.file.pos; X_M32(info + 88) = 0x20; X_M32(info + 92) = 0; written = 96; } break;
    default: XK_LOG("NtQueryInformationFile: class %u unsupported\n", cls); st = STATUS_INVALID_INFO_CLASS; break;
    }
    if (iosb) { IOSB_STATUS(iosb) = st; IOSB_INFO(iosb) = written; }
    c->r[0] = st; X_RET(5);
}
void xk_NtSetInformationFile(xctx *c)
{
    xk_obj *o = file_from_handle(X_ARG(0)); uint32_t iosb = X_ARG(1), info = X_ARG(2), cls = X_ARG(4);
    uint32_t st = STATUS_SUCCESS;
    if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(5); }
    switch (cls) {
    case FileDispositionInformation: o->u.file.delete_on_close = X_M8(info) != 0; break;
    case FilePositionInformation: o->u.file.pos = LI64(info); break;
    case FileEndOfFileInformation: case FileAllocationInformation: {
        /* The aligned(1) uint64_t lvalue type in LI64 leaks into Vita GCC's
         * argument layout: it passes r1/r2, but the adapter expects r2/r3.
         * Assemble two words to obtain an ordinary uint64_t at this ABI boundary.
         * A cast or a plain uint64_t temporary still gets miscompiled. */
        uint64_t size = (uint64_t)X_M32(info) | ((uint64_t)X_M32(info + 4) << 32);
        if (o->u.file.f && xk_os_truncate(o->u.file.f, size) != 0) st = STATUS_ACCESS_DENIED;
        break;
    }
    case FileBasicInformation: break;
    case FileRenameInformation: {   /* { BOOLEAN Replace; HANDLE RootDir; ANSI_STRING FileName } (Xbox) */
        char nm[512]; xk_ansi_to_c(info + 8, nm, sizeof nm); char *host = xk_path_translate(nm, X_M32(info + 4) ? X_M32(info + 4) : OB_DOS_DEVICES_DIRECTORY);
        if (!host) host = xk_path_translate(nm, 0);
        if (host) { if (xk_os_rename(o->u.file.path, host) != 0) st = STATUS_ACCESS_DENIED; else { free(o->u.file.path); o->u.file.path = host; host = NULL; } free(host); } else st = STATUS_OBJECT_PATH_NOT_FOUND; break; }
    default: XK_LOG("NtSetInformationFile: class %u unsupported\n", cls); st = STATUS_INVALID_INFO_CLASS; break;
    }
    if (iosb) { IOSB_STATUS(iosb) = st; IOSB_INFO(iosb) = 0; }
    c->r[0] = st; X_RET(5);
}
/* NTSTATUS NtQueryFullAttributesFile(POBJECT_ATTRIBUTES, PFILE_NETWORK_OPEN_INFORMATION) */
void xk_NtQueryFullAttributesFile(xctx *c)
{
    uint32_t oa = X_ARG(0), info = X_ARG(1);
    char name[512]; xk_ansi_to_c(OA_NAME(oa), name, sizeof name);
    char *host = xk_path_translate(name, OA_ROOT(oa));
    int is_dir = 0; uint64_t size = 0, mtime = 0;
    if (!host || stat_ci(host, &is_dir, &size, &mtime) != 0) { free(host); c->r[0] = STATUS_OBJECT_NAME_NOT_FOUND; X_RET(2); }
    free(host);
    fill_basic(info, mtime, is_dir); LI64(info + 32) = (size + 4095) & ~4095ull; LI64(info + 40) = size; X_M32(info + 48) = is_dir ? 0x10 : 0x80;
    c->r[0] = STATUS_SUCCESS; X_RET(2);
}
/* NTSTATUS NtQueryVolumeInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FS_INFORMATION_CLASS) */
void xk_NtQueryVolumeInformationFile(xctx *c)
{
    xk_obj *o = file_from_handle(X_ARG(0)); uint32_t iosb = X_ARG(1), info = X_ARG(2), cls = X_ARG(4);
    uint32_t st = STATUS_SUCCESS, written = 0;
    if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(5); }
    uint64_t freeb = 1ull << 30, total = 8ull << 30;
    xk_os_freespace(o->u.file.path, &freeb, &total);
    switch (cls) {
    case 1: /* FileFsVolumeInformation */ LI64(info) = 0; X_M32(info + 8) = 0x12345678; X_M32(info + 12) = 0; X_M8(info + 16) = 0; written = 18; break;
    case 3: /* FileFsSizeInformation: TotalAllocationUnits, AvailableAllocationUnits, SectorsPerAllocationUnit, BytesPerSector */
        LI64(info) = total / 16384; LI64(info + 8) = freeb / 16384; X_M32(info + 16) = 32; X_M32(info + 20) = 512; written = 24; break;
    case 4: /* FileFsDeviceInformation */ X_M32(info) = strstr(o->u.file.path, "haloce") ? 2 /* CD_ROM */ : 7 /* DISK */; X_M32(info + 4) = 0; written = 8; break;
    case 5: /* FileFsAttributeInformation */ X_M32(info) = 0x00000002 | 0x00000008; X_M32(info + 4) = 42; X_M32(info + 8) = 4; memcpy(X_G(info + 12), "FATX", 4); written = 16; break;
    default: st = STATUS_INVALID_INFO_CLASS; break;
    }
    if (iosb) { IOSB_STATUS(iosb) = st; IOSB_INFO(iosb) = written; }
    c->r[0] = st; X_RET(5);
}
/* NTSTATUS NtQueryDirectoryFile(HANDLE, HANDLE Event, PIO_APC_ROUTINE, PVOID Ctx, PIO_STATUS_BLOCK, PVOID Info, ULONG Length, FILE_INFORMATION_CLASS, PANSI_STRING FileName, BOOLEAN RestartScan) - Xbox: 10 args? (3925: 8 + FileName + RestartScan) */
static int match(const char *pat, const char *name)
{
    if (!pat || !*pat || !strcmp(pat, "*") || !strcmp(pat, "*.*")) return 1;
    if (*pat == '*') { for (const char *n = name; ; ++n) { if (match(pat + 1, n)) return 1; if (!*n) return 0; } }
    if (*pat == '?') return *name && match(pat + 1, name + 1);
    return tolower((unsigned char)*pat) == tolower((unsigned char)*name) && (*pat ? match(pat + 1, name + 1) : 1);
}
void xk_NtQueryDirectoryFile(xctx *c)
{
    xk_obj *o = file_from_handle(X_ARG(0)); uint32_t iosb = X_ARG(4), info = X_ARG(5), len = X_ARG(6), cls = X_ARG(7), pat = X_ARG(8); int restart = X_ARG(9) & 0xFF;
    if (!o || o->type != XO_DIRECTORY) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(10); }
    if (pat && !o->u.file.pattern) { char p[260]; xk_ansi_to_c(pat, p, sizeof p); o->u.file.pattern = strdup(p); }
    if (restart && o->u.file.dir) { xk_os_closedir(o->u.file.dir); o->u.file.dir = NULL; }
    if (!o->u.file.dir) o->u.file.dir = xk_os_opendir(o->u.file.path);
    uint32_t st = STATUS_NO_MORE_FILES, written = 0;
    char name[512]; int is_dir; uint64_t size;
    while (o->u.file.dir && xk_os_readdir(o->u.file.dir, name, sizeof name, &is_dir, &size)) {
        if (!match(o->u.file.pattern, name)) continue;
        uint32_t n = (uint32_t)strlen(name);
        if (cls == FileDirectoryInformation) {          /* { NextEntryOffset, FileIndex, Create/Access/Write/Change, EndOfFile, Allocation, Attributes, FileNameLength, FileName[] } */
            if (len < 64 + n) { st = STATUS_BUFFER_OVERFLOW; break; }
            memset(X_G(info), 0, 64); uint64_t mt = 0; char full[1024]; snprintf(full, sizeof full, "%s/%s", o->u.file.path, name); xk_os_stat(full, NULL, NULL, &mt);
            LI64(info + 8) = mt; LI64(info + 16) = mt; LI64(info + 24) = mt; LI64(info + 32) = mt; LI64(info + 40) = size; LI64(info + 48) = (size + 4095) & ~4095ull;
            X_M32(info + 56) = is_dir ? 0x10 : 0x80; X_M32(info + 60) = n; memcpy(X_G(info + 64), name, n); written = 64 + n;
        } else if (cls == FileNamesInformation) {       /* { NextEntryOffset, FileIndex, FileNameLength, FileName[] } */
            if (len < 12 + n) { st = STATUS_BUFFER_OVERFLOW; break; }
            X_M32(info) = 0; X_M32(info + 4) = 0; X_M32(info + 8) = n; memcpy(X_G(info + 12), name, n); written = 12 + n;
        } else { st = STATUS_INVALID_INFO_CLASS; break; }
        st = STATUS_SUCCESS; break;
    }
    if (iosb) { IOSB_STATUS(iosb) = st; IOSB_INFO(iosb) = written; }
    /* XV_SAVE_LOG: trace enumeration of the save tree so a failed campaign-resume shows what the game found */
    { static int on = -1; if (on < 0) { const char *e = getenv("XV_SAVE_LOG"); on = e ? atoi(e) : 0; }
      if (on && o->u.file.path && (strstr(o->u.file.path, "save") || strstr(o->u.file.path, "saved")))
          XK_LOG("[save] QueryDir %s pat=%s -> %s '%s'\n", o->u.file.path, o->u.file.pattern ? o->u.file.pattern : "*", st == STATUS_SUCCESS ? "entry" : (st == STATUS_NO_MORE_FILES ? "no-more" : "err"), st == STATUS_SUCCESS ? name : ""); }
    c->r[0] = st; X_RET(10);
}
void xk_NtFsControlFile(xctx *c) { XK_LOG("NtFsControlFile(code %08X) ignored\n", X_ARG(5)); if (X_ARG(4)) { IOSB_STATUS(X_ARG(4)) = STATUS_SUCCESS; IOSB_INFO(X_ARG(4)) = 0; } c->r[0] = STATUS_SUCCESS; X_RET(10); }
void xk_NtDeviceIoControlFile(xctx *c) { XK_LOG("NtDeviceIoControlFile(code %08X) ignored\n", X_ARG(5)); if (X_ARG(4)) { IOSB_STATUS(X_ARG(4)) = STATUS_INVALID_DEVICE_REQUEST; } c->r[0] = STATUS_INVALID_DEVICE_REQUEST; X_RET(10); }
void xk_NtDeleteFile(xctx *c) { char nm[512]; xk_ansi_to_c(OA_NAME(X_ARG(0)), nm, sizeof nm); char *h = xk_path_translate(nm, OA_ROOT(X_ARG(0))); c->r[0] = h && xk_os_unlink(h) == 0 ? STATUS_SUCCESS : STATUS_OBJECT_NAME_NOT_FOUND; free(h); X_RET(1); }

/* ---- symbolic links -------------------------------------------------------------------- */
void xk_IoCreateSymbolicLink(xctx *c)
{
    char nm[256], tg[256]; xk_ansi_to_c(X_ARG(0), nm, sizeof nm); xk_ansi_to_c(X_ARG(1), tg, sizeof tg);
    const char *n = strncasecmp(nm, "\\??\\", 4) == 0 ? nm + 4 : nm;
    xk_path_add_link(n, tg);
    XK_LOG("IoCreateSymbolicLink \\??\\%s -> %s\n", n, tg);
    c->r[0] = STATUS_SUCCESS; X_RET(2);
}
void xk_IoDeleteSymbolicLink(xctx *c) { c->r[0] = STATUS_SUCCESS; X_RET(1); }
void xk_NtOpenSymbolicLinkObject(xctx *c)
{
    uint32_t oa = X_ARG(1); char nm[256]; xk_ansi_to_c(OA_NAME(oa), nm, sizeof nm);
    const char *n = strncasecmp(nm, "\\??\\", 4) == 0 ? nm + 4 : nm;
    const char *t = link_lookup(n);
    if (!t) { c->r[0] = STATUS_OBJECT_NAME_NOT_FOUND; X_RET(2); }
    xk_obj *o = xk_obj_new(XO_SYMLINK); o->u.symlink.target = strdup(t);
    X_M32(X_ARG(0)) = xk_handle_create(o); c->r[0] = STATUS_SUCCESS; X_RET(2);
}
/* NTSTATUS NtQuerySymbolicLinkObject(HANDLE, PANSI_STRING Target, PULONG ReturnedLength) */
void xk_NtQuerySymbolicLinkObject(xctx *c)
{
    xk_obj *o = xk_handle_get_type(X_ARG(0), XO_SYMLINK); uint32_t s = X_ARG(1);
    if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(3); }
    uint32_t n = (uint32_t)strlen(o->u.symlink.target);
    if (X_ARG(2)) X_M32(X_ARG(2)) = n;
    if (AS_MAX(s) < n) { c->r[0] = STATUS_BUFFER_TOO_SMALL; X_RET(3); }
    memcpy(X_G(AS_BUF(s)), o->u.symlink.target, n); AS_LEN(s) = n; if (AS_MAX(s) > n) X_M8(AS_BUF(s) + n) = 0;
    c->r[0] = STATUS_SUCCESS; X_RET(3);
}
void xk_IoDismountVolume(xctx *c) { c->r[0] = STATUS_SUCCESS; X_RET(1); }
void xk_IoDismountVolumeByName(xctx *c) { c->r[0] = STATUS_SUCCESS; X_RET(1); }

/* called by the handle table when the last reference to a file object drops */
void xk_file_release(xk_obj *o)
{
    if (o->u.file.f) xk_os_close(o->u.file.f);
    if (o->u.file.dir) xk_os_closedir(o->u.file.dir);
    if (o->u.file.delete_on_close && o->u.file.path) { if (o->u.file.is_dir) xk_os_rmdir(o->u.file.path); else xk_os_unlink(o->u.file.path); XK_LOG("deleted %s\n", o->u.file.path); }
    free(o->u.file.path); free(o->u.file.pattern);
}
