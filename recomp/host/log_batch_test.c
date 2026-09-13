/* Actual Vita log sink with mocked syscalls: preserve long reports and retry
 * short file writes while holding a single lock. No device is accessed. */
#define __vita__ 1
#include <assert.h>
#include <string.h>
#include "../../runtime/xv_log.c"

static char console_text[32768], file_text[32768];
static unsigned console_n, file_n, writes, locks, unlocks, short_limit;
static int held, fail_write, zero_write;

int sceClibPrintf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(console_text + console_n, sizeof console_text - console_n, fmt, ap);
    va_end(ap);
    assert(n >= 0 && n <= 511); console_n += (unsigned)n; return n;
}
SceSSize sceIoWrite(SceUID fd, const void *text, SceSize size)
{
    assert(fd == 10 && held); writes++;
    if (fail_write) return -1;
    if (zero_write) return 0;
    if (short_limit && size > short_limit) size = short_limit;
    assert(size <= sizeof file_text - file_n);
    memcpy(file_text + file_n, text, size); file_n += size; return size;
}
SceUID sceKernelCreateMutex(const char *name, SceUInt attr, int count, SceKernelMutexOptParam *opt)
{ (void)name; (void)attr; (void)count; (void)opt; return 20; }
int sceKernelLockMutex(SceUID id, int count, unsigned *timeout)
{ assert(id == 20 && count == 1 && !timeout && !held); held = 1; locks++; return 0; }
int sceKernelUnlockMutex(SceUID id, int count)
{ assert(id == 20 && count == 1 && held); held = 0; unlocks++; return 0; }
int sceIoMkdir(const char *p, SceMode mode) { (void)p; (void)mode; return 0; }
int sceIoGetstat(const char *p, SceIoStat *s) { (void)p; (void)s; return -1; }
int sceIoRename(const char *a, const char *b) { (void)a; (void)b; return 0; }
int sceIoRemove(const char *p) { (void)p; return 0; }
SceUID sceIoOpen(const char *p, int flags, SceMode mode)
{ (void)p; (void)flags; (void)mode; return 10; }
int sceIoSyncByFd(SceUID fd, int flag) { assert(fd == 10 && !flag); return 0; }

static void reset(void)
{
    console_n = file_n = writes = locks = unlocks = short_limit = 0;
    held = fail_write = zero_write = 0; g_fd = 10; g_mtx = 20;
    memset(console_text, 0, sizeof console_text); memset(file_text, 0, sizeof file_text);
}
int main(void)
{
    char report[15000];
    for (unsigned i = 0; i < sizeof report; i++) report[i] = i % 97 ? 'a' + i % 26 : '\n';
    reset(); xv_log_write(report, sizeof report);
    assert(writes == 1 && locks == 1 && unlocks == 1 && !held);
    assert(console_n == sizeof report && file_n == sizeof report);
    assert(!memcmp(report, file_text, sizeof report) && !memcmp(report, console_text, sizeof report));
    reset(); short_limit = 37; xv_log_write(report, sizeof report);
    assert(writes == (sizeof report + 36) / 37 && locks == 1 && unlocks == 1);
    assert(file_n == sizeof report && !memcmp(report, file_text, sizeof report));
    reset(); fail_write = 1; xv_log_write(report, sizeof report);
    assert(writes == 1 && !file_n && !held && unlocks == 1);
    reset(); zero_write = 1; xv_log_write(report, sizeof report);
    assert(writes == 1 && !file_n && !held && unlocks == 1);
    reset(); xv_log_write(NULL, 1); xv_log_write(report, 0);
    assert(!writes && !locks && !console_n);
    reset(); xv_logf("plain %u %s\n", 42u, "record");
    assert(!strcmp(console_text, "plain 42 record\n"));
    assert(console_n == file_n && !memcmp(console_text, file_text, file_n));
    reset(); g_fd = -2; g_mtx = -1; xv_logf("initial record\n");
    assert(g_fd == 10 && g_mtx == 20 && writes == 1 && !held);
    puts("PASS: full batch, one file write/lock, short writes, failures and ordinary log compatibility");
    return 0;
}
