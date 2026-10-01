#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../kernel/xk_read_retry.h"

static unsigned calls, chunk_limit, fail_at, zero_first;
static uint8_t source[100000];
static int64_t fake_read(void *unused, uint64_t pos, void *dst, uint32_t n)
{
    (void)unused;
    if (++calls == fail_at) return -EIO;
    if (calls == 1 && zero_first) return 0;
    if (pos >= sizeof source) return 0;
    if (n > sizeof source - pos) n = sizeof source - pos;
    if (chunk_limit && n > chunk_limit) n = chunk_limit;
    memcpy(dst, source + pos, n);
    return n;
}

static void partial_reads(void)
{
    uint8_t out[100002];
    for (unsigned i = 0; i < sizeof source; ++i) source[i] = (i * 31u) ^ (i >> 8);
    const unsigned limits[] = {0, 1, 511, 32768, 65536};
    for (unsigned l = 0; l < sizeof limits / sizeof limits[0]; ++l) {
        memset(out, 0xCD, sizeof out); calls = 0; fail_at = 0; chunk_limit = limits[l];
        unsigned retries;
        assert(xk_read_retry(NULL, 0, out + 1, sizeof source, fake_read, &retries) == sizeof source);
        assert(!memcmp(out + 1, source, sizeof source));
        assert(out[0] == 0xCD && out[sizeof out - 1] == 0xCD);
        assert(retries == calls - 1);
        if (!chunk_limit) assert(calls == 1);
    }
    unsigned retries;
    calls = 0; chunk_limit = 0; zero_first = 1;
    assert(xk_read_retry(NULL, 29, out, 100, fake_read, &retries) == 100);
    assert(!memcmp(out, source + 29, 100) && retries == 1);
    zero_first = 0;
    calls = 0; chunk_limit = 17; fail_at = 3; memset(out, 0xCD, sizeof out);
    assert(xk_read_retry(NULL, 5, out, 100, fake_read, &retries) == 34);
    assert(!memcmp(out, source + 5, 34) && out[34] == 0xCD);
    calls = 0; fail_at = 1;
    assert(xk_read_retry(NULL, 0, out, 100, fake_read, &retries) == -EIO && retries == 0);
    calls = 0; fail_at = 0; chunk_limit = 0; memset(out, 0xCD, sizeof out);
    assert(xk_read_retry(NULL, sizeof source - 7, out, 100, fake_read, &retries) == 7);
    assert(!memcmp(out, source + sizeof source - 7, 7) && out[7] == 0xCD);
    assert(xk_read_retry(NULL, sizeof source, out, 100, fake_read, &retries) == 0);
    assert(xk_read_retry(NULL, 0, out, 0, fake_read, &retries) == 0 && retries == 0);
}

/* Reproduce the actual Linux behavior behind a GPU-tracked destination: pread
 * stops at a protected page without delivering SIGSEGV. A CPU copy invokes the
 * tracking handler and lets it make that page writable. */
static uint8_t *protected_page;
static size_t page_size;
static volatile sig_atomic_t faults;
static void tracking_fault(int sig, siginfo_t *info, void *unused)
{
    (void)unused;
    uintptr_t a = (uintptr_t)info->si_addr, lo = (uintptr_t)protected_page;
    if (sig != SIGSEGV || a < lo || a >= lo + page_size) _exit(90);
    if (mprotect(protected_page, page_size, PROT_READ | PROT_WRITE)) _exit(91);
    ++faults;
}
static int64_t posix_read(void *handle, uint64_t pos, void *dst, uint32_t n)
{
    return pread(*(int *)handle, dst, n, (off_t)pos);
}
static void protected_destination(void)
{
    page_size = (size_t)sysconf(_SC_PAGESIZE);
    assert(page_size * 3 < sizeof source);
    uint8_t *dst = mmap(NULL, page_size * 3, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(dst != MAP_FAILED);
    FILE *file = tmpfile(); assert(file);
    assert(fwrite(source, 1, page_size * 3, file) == page_size * 3);
    assert(!fflush(file)); int fd = fileno(file);
    protected_page = dst + page_size;
    assert(!mprotect(protected_page, page_size, PROT_READ));
    assert(pread(fd, dst, page_size * 3, 0) == (ssize_t)page_size);
    struct sigaction handler = {0}, previous;
    handler.sa_sigaction = tracking_fault; handler.sa_flags = SA_SIGINFO;
    sigemptyset(&handler.sa_mask); assert(!sigaction(SIGSEGV, &handler, &previous));
    unsigned retries;
    assert(xk_read_retry(&fd, 0, dst, page_size * 3, posix_read, &retries) == (int64_t)page_size * 3);
    assert(retries > 0 && faults == 1 && !memcmp(dst, source, page_size * 3));
    assert(!sigaction(SIGSEGV, &previous, NULL));
    munmap(dst, page_size * 3); fclose(file);
}
int main(void)
{
    partial_reads(); protected_destination();
    puts("Read retry: direct/partial/EOF/error/guards and real protected-page recovery passed");
    return 0;
}
