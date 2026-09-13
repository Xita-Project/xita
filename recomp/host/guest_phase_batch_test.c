/* Worst-case report width and the actual batched phase emission path. */
#include <assert.h>
#include <limits.h>
#include "../xv_phase.c"

#define T(n) {n, "abcdefghijklmnopqrstuvwxabcdefghijklmnopqrstuvwx"}
const xv_phase_target xv_phase_targets[] = {
    T(1),T(2),T(3),T(4),T(5),T(6),T(7),T(8),T(9),T(10),T(11),T(12),
    T(13),T(14),T(15),T(16),T(17),T(18),T(19),T(20),T(21),T(22),T(23),T(24),
    T(25),T(26),T(27),T(28),T(29),T(30),T(31),T(32),T(33),T(34),T(35),T(36),
    T(37),T(38),T(39),T(40),T(41),T(42),T(43),T(44),T(45),T(46),T(47),T(48)
};
#undef T
const unsigned xv_phase_target_count = 48;
static unsigned batches, plain_records;
uint64_t xk_os_monotonic_us(void) { return UINT64_MAX; }
void xk_os_log(const char *fmt, ...)
{
    /* Only initialization and the separately measured cost record use the
     * ordinary logger. No rows should fall back to individual file writes. */
    assert(strstr(fmt, "compiled scopes") || strstr(fmt, "report-us"));
    plain_records++;
}
void xk_os_log_batch(const char *text, unsigned length)
{
    batches++;
    assert(length == strlen(text) && length > 10000 && length < sizeof report_buffer);
    unsigned lines = 0;
    for (const char *p = text; *p; ++p) if (*p == '\n') lines++;
    assert(lines == 49);
    assert(strstr(text, "00000030 abcdefghijklmnopqrstuvwxabcdefghijklmnopqrstuvwx calls 18446744073709551615"));
}
int main(void)
{
    setenv("XV_PHASE_TIMING", "1", 1); xv_phase_init(); assert(xv_phase_enabled);
    for (unsigned i = 0; i < target_count; i++) {
        stats[i] = (phase_stat){UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
    }
    for (unsigned i = 0; i < 60; i++) xv_phase_frame(UINT_MAX);
    assert(batches == 1 && plain_records == 2);
    for (unsigned i = 0; i < target_count; i++) assert(!stats[i].active && !stats[i].calls);
    puts("PASS: maximum-width phase window, complete final row and one batched emission");
    return 0;
}
