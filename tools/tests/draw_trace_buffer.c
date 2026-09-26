/* cc -pthread -Iruntime tools/tests/draw_trace_buffer.c -o /tmp/trace-test */
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned writes, bytes, fail_alloc, allocations;
static pthread_t owner;
static char output[128];
static void *test_malloc(size_t n) { ++allocations; return fail_alloc ? NULL : malloc(n); }
static void xv_log_critical_write(const char *p, unsigned n)
{
    assert(pthread_equal(pthread_self(), owner));
    assert(n < sizeof output); memcpy(output, p, n); output[n] = 0;
    ++writes; bytes += n;
}
static void xv_log_criticalf(const char *fmt, ...) { (void)fmt; assert(pthread_equal(pthread_self(), owner)); }
#define XV_DRAW_TRACE_BYTES 64
#define malloc test_malloc
#include "xv_draw_trace_buffer.h"
#undef malloc
static void *worker(void *unused)
{
    (void)unused;
    assert(xv_draw_trace_append("draw %u\n", 42));
    assert(!writes); /* Worker never accesses the file sink. */
    return NULL;
}
int main(void)
{
    owner = pthread_self();
    xv_draw_trace_begin(0); assert(!allocations);
    assert(!xv_draw_trace_append("ignored")); xv_draw_trace_end(1);
    assert(!writes);
    xv_draw_trace_begin(1);
    pthread_t thread; assert(!pthread_create(&thread, NULL, worker, NULL));
    assert(!pthread_join(thread, NULL));
    assert(!xv_draw_trace_append("%080d", 7)); /* Partial line never emitted. */
    assert(xv_draw_trace.dropped == 1);
    assert(xv_draw_trace_append("end\n"));
    xv_draw_trace_end(2);
    assert(!strcmp(output, "draw 42\nend\n") && bytes == 12 && writes == 1);
    assert(!xv_draw_trace.text && !xv_draw_trace.active);
    fail_alloc = 1; xv_draw_trace_begin(1);
    assert(!xv_draw_trace_append("lost\n") && xv_draw_trace.dropped == 1);
    xv_draw_trace_end(3); assert(writes == 1);
    fail_alloc = 0; xv_draw_trace_begin(1);
    assert(xv_draw_trace_append("%063d", 1));
    assert(!xv_draw_trace_append("x"));
    xv_draw_trace_end(4); assert(writes == 2 && bytes == 75);
    puts("draw trace: worker handoff, overflow, allocation failure and disabled path pass");
}
