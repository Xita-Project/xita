#include <assert.h>
#include <stdarg.h>
#include "../xv_funchist.c"
static char output[2048];
static unsigned starts;
/* The generated dispatch table supplies a const build marker in the app. */
#ifndef XV_TEST_GUEST_TRACE
#define XV_TEST_GUEST_TRACE 0
#endif
const unsigned xv_guest_trace_enabled = XV_TEST_GUEST_TRACE;
void xk_os_log(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    size_t n = strlen(output); vsnprintf(output + n, sizeof output - n, fmt, ap);
    va_end(ap);
}
void xk_os_sleep_us(uint64_t us) { (void)us; }
int xk_os_audio_thread_start(void (*fn)(void *), void *arg) { (void)fn; (void)arg; starts++; return 0; }
int main(void)
{
    unsigned automatic = XV_TEST_GUEST_TRACE != 0;
    unsetenv("XV_WATCH_FN");
    assert(xv_watch_n == -1);
    unsetenv("XV_PROF"); xv_prof_start(); assert(starts == automatic);
    assert(xv_watch_n == 0);
    assert(strstr(output, automatic ? "sampling profiler on" : "sampling profiler off"));
    assert(strstr(output, automatic ? "guest function tracing enabled" : "guest function tracing absent"));
    output[0] = 0;
    setenv("XV_PROF", "0", 1); xv_prof_start(); assert(starts == automatic);
    assert(strstr(output, "sampling profiler off"));
    assert(strstr(output, automatic ? "guest function tracing enabled" : "guest function tracing absent"));
    output[0] = 0;
    setenv("XV_PROF", "1", 1); xv_prof_start(); assert(starts == automatic + 1);
    assert(strstr(output, "sampling profiler on"));
    assert(strstr(output, automatic ? "guest function tracing enabled" : "guest function tracing absent"));
    prof_sample(0); prof_sample(0); prof_sample(0); prof_sample(0x80001234);
    prof_dump();
    assert(strstr(output, "4 samples, 2 functions"));
    assert(strstr(output, "unattributed:75.0%") && strstr(output, "H1234:25.0%"));
    output[0] = 0; prof_sample(0x1234); prof_dump();
    assert(strstr(output, "1 samples, 1 functions") && strstr(output, "1234:100.0%"));
    /* Startup must also preserve an explicitly requested watch list. */
    setenv("XV_WATCH_FN", "B71C0,1D130", 1); xv_watch_n = -1;
    setenv("XV_PROF", "0", 1); xv_prof_start();
    assert(xv_watch_n == 2 && xv_watch[0] == 0xB71C0 && xv_watch[1] == 0x1D130);
    puts("PASS: traced build defaults, explicit profiler override, unattributed samples, percentages and interval reset");
}
