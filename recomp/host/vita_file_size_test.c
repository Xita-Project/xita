/* Real Vita adapter over host files: persistent save growth and sparse caches. */
#define __vita__ 1
#include "../kernel/xk_os_vita.c"
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail_write;
void xv_logf(const char *fmt, ...) { (void)fmt; }
SceUInt64 sceKernelGetProcessTimeWide(void) { return 0; }
SceUID sceIoOpen(const char *path, int flags, SceMode mode)
{
    int host_flags = (flags & SCE_O_RDWR) == SCE_O_RDWR ? O_RDWR : O_RDONLY;
    if (flags & SCE_O_CREAT) host_flags |= O_CREAT;
    if (flags & SCE_O_TRUNC) host_flags |= O_TRUNC;
    return open(path, host_flags, mode);
}
int sceIoClose(SceUID fd) { return close(fd); }
SceOff sceIoLseek(SceUID fd, SceOff off, int whence) { return lseek(fd, off, whence); }
int sceIoPwrite(SceUID fd, const void *buf, SceSize n, SceOff off)
{ return fail_write ? -1 : (int)pwrite(fd, buf, n, off); }
int sceIoPread(SceUID fd, void *buf, SceSize n, SceOff off)
{ return (int)pread(fd, buf, n, off); }
int sceIoGetstat(const char *path, SceIoStat *st)
{
    struct stat host; if (stat(path, &host)) return -1;
    memset(st, 0, sizeof *st); st->st_size = host.st_size;
    st->st_mode = S_ISDIR(host.st_mode) ? SCE_S_IFDIR : SCE_S_IFREG;
    return 0;
}
int sceRtcGetTick(const SceDateTime *time, SceRtcTick *tick)
{ (void)time; tick->tick = 0; return 0; }

static uint64_t disk_size(const char *path)
{ struct stat st; assert(!stat(path, &st)); return st.st_size; }
static void expect_size(xk_file *f, uint64_t size)
{
    uint64_t reported = 0;
    assert(xk_os_size(f) == (int64_t)size);
    assert(!xk_os_stat(f->path, NULL, &reported, NULL) && reported == size);
}
int main(void)
{
    char dir[] = "/tmp/xita-files-XXXXXX"; assert(mkdtemp(dir));
    char save[128], cache_dir[128], cache[128];
    snprintf(save, sizeof save, "%s/save", dir); assert(!mkdir(save, 0700));
    snprintf(cache_dir, sizeof cache_dir, "%s/save/cache", dir); assert(!mkdir(cache_dir, 0700));
    snprintf(save, sizeof save, "%s/save/savegame.bin", dir);
    snprintf(cache, sizeof cache, "%s/save/cache/cache000.map", dir);
    xk_file *f = xk_os_open(save, 1, 1, 0, NULL); assert(f);
    const unsigned char header[] = {0x48, 0x41, 0x4C, 0x4F, 1, 2, 3, 4};
    assert(xk_os_write(f, 0, header, sizeof header) == sizeof header);
    assert(!xk_os_truncate(f, 0x380000));
    expect_size(f, 0x380000); assert(disk_size(save) == 0x380000);
    assert(xk_os_write(f, 0x345000 - 1, header, 1) == 1);
    expect_size(f, 0x380000); xk_os_close(f);
    memset(g_lsize, 0, sizeof g_lsize); /* App restart loses every logical entry. */
    f = xk_os_open(save, 1, 0, 0, NULL); assert(f); expect_size(f, 0x380000);
    unsigned char got[16]; memset(got, 0xCC, sizeof got);
    assert(xk_os_read(f, 0, got, sizeof header) == sizeof header);
    assert(!memcmp(got, header, sizeof header));
    assert(xk_os_read(f, 0x37FFF0, got, sizeof got) == sizeof got);
    for (unsigned i = 0; i < sizeof got; i++) assert(!got[i]);
    fail_write = 1; assert(xk_os_truncate(f, 0x400000) < 0); fail_write = 0;
    expect_size(f, 0x380000); assert(disk_size(save) == 0x380000);
    assert(xk_os_truncate(f, UINT64_MAX) < 0); expect_size(f, 0x380000);
    /* Existing shrink behavior stays logical, including path-based queries. */
    assert(!xk_os_truncate(f, 32)); assert(xk_os_size(f) == 32);
    assert(disk_size(save) == 0x380000);
    assert(!xk_os_truncate(f, 0x380000)); expect_size(f, 0x380000);
    xk_os_close(f);
    f = xk_os_open(save, 0, 0, 0, NULL); assert(f);
    assert(xk_os_truncate(f, 0x400000) < 0); expect_size(f, 0x380000); xk_os_close(f);
    /* Map caches retain their existing logical preallocation and physical stat size. */
    f = xk_os_open(cache, 1, 1, 0, NULL); assert(f);
    xk_file *other = xk_os_open(cache, 1, 0, 0, NULL); assert(other);
    assert(xk_os_write(f, 0, header, sizeof header) == sizeof header);
    assert(!xk_os_truncate(f, 49u << 20));
    assert(disk_size(cache) == sizeof header);
    assert(xk_os_size(f) == 49u << 20);
    uint64_t cache_size; assert(!xk_os_stat(cache, NULL, &cache_size, NULL));
    assert(cache_size == sizeof header); /* Preserve the existing map-cache query behavior. */
    xk_os_close(other); other = xk_os_open(cache, 1, 0, 0, NULL); assert(other);
    assert(xk_os_size(other) == 49u << 20);
    memset(got, 0xCC, sizeof got);
    assert(xk_os_read(other, 1u << 20, got, sizeof got) == sizeof got);
    for (unsigned i = 0; i < sizeof got; i++) assert(!got[i]);
    xk_os_close(f); xk_os_close(other); assert(!g_fds_live);
    assert(!unlink(save) && !unlink(cache) && !rmdir(cache_dir));
    snprintf(save, sizeof save, "%s/save", dir); assert(!rmdir(save) && !rmdir(dir));
    puts("PASS: save reservation survives reopen/restart, preserves header, zero tail, failures, existing sparse map cache behavior");
}
