#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../xv_draw_profile.h"

static unsigned clock_calls, reports;
static uint64_t now;
static char report[1024];
#ifndef XV_TEST_NO_CLOCK
uint64_t xk_os_monotonic_us(void) { clock_calls++; return now; }
#endif
void xv_logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(report, sizeof report, fmt, ap);
    va_end(ap);
    reports++;
}
int main(int argc, char **argv)
{
    int disabled = argc > 1;
    if (disabled) setenv("XV_DRAW_PROFILE", "0", 1);
    else unsetenv("XV_DRAW_PROFILE");
#ifdef XV_TEST_NO_CLOCK
    disabled = 1;
#endif
    uint64_t stamp = xv_draw_profile_begin();
    if (disabled) {
        assert(!stamp);
        now = 10000;
        xv_draw_profile_step(XV_DRAW_INDICES, &stamp);
        xv_draw_profile_report(60);
        assert(!clock_calls && !reports);
    } else {
        assert(stamp); /* a clock starting at zero must still be enabled */
        now = 1000; xv_draw_profile_step(XV_DRAW_SETUP, &stamp);
        now = 3000; xv_draw_profile_step(XV_DRAW_INDICES, &stamp);
        now = 6000; xv_draw_profile_step(XV_DRAW_STREAMS, &stamp);
        xv_draw_profile_report(0); assert(!reports);
        xv_draw_profile_report(2);
        assert(strstr(report, "2 frames 1 draws: setup 0.500 state 0.000 indices 1.000"));
        assert(strstr(report, "streams 1.500"));
        xv_draw_profile_report(2);
        assert(strstr(report, "2 frames 0 draws: setup 0.000"));
        assert(strstr(report, "streams 0.000"));
        assert(clock_calls == 4 && reports == 2);
    }
    puts("PASS: draw profiling accounting/reset, zero clock origin, and disabled/no-clock behavior");
    return 0;
}
