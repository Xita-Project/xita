/* Geometry-only oracle adapter. This intentionally is NOT a game ABI hook. */
#include "xv_x86rt.h"
#include "portal_polygon.h"

int *__errno(void) { static int e; return &e; }
int snprintf(char *d, size_t n, const char *f, ...)
{ (void)d; (void)n; (void)f; __builtin_trap(); }
unsigned long strtoul(const char *s, char **e, int b)
{ (void)s; (void)e; (void)b; __builtin_trap(); }
void sceClibPrintf(const char *f, ...) { (void)f; __builtin_trap(); }

void test_candidate(xctx *c)
{
    xp_point input[256], boundary[256], output[256], scratch[512];
    uint32_t sp = c->r[4];
    int n = (int16_t)c->r[1], edges = (int16_t)X_M32(sp+4);
    int capacity = (int16_t)X_M32(sp+12);
    if (n < 1 || n > 256 || edges < 1 || edges > 256
        || capacity < 1 || capacity > 256) __builtin_trap();
    float tolerance;
    x_guest_read(&tolerance, sp+20, 4);
    x_guest_read(input, c->r[2], n*sizeof(*input));
    x_guest_read(boundary, X_M32(sp+8), edges*sizeof(*boundary));
    xp_work work;
    int result = xp_portal_polygon(input, n, boundary, edges, capacity,
                                   tolerance, output, scratch, &work);
    if (result > 0) x_guest_write(X_M32(sp+16), output, result*sizeof(*output));
    c->r[0] = (uint16_t)result;
    c->preempt -= work.backedges;
}

#ifdef XP_WHOLE_VISIBILITY
unsigned xp_typed_calls, xp_fallback_calls;
void __real_f_000B7F10(xctx *c);
/* Experiment at the one inspected portal call boundary. Deliberately omits
 * old internal scheduler events and dead scratch/register transport. Whole
 * traversal output comparison is necessary but not sufficient for deployment.
 */
void __wrap_f_000B7F10(xctx *c)
{
    uint32_t sp = c->r[4];
    int n = (int16_t)c->r[1], edges = (int16_t)X_M32(sp+4);
    int capacity = (int16_t)X_M32(sp+12);
    if (X_M32(sp) != 0x534DA || n < 1 || n > 256 || edges < 1 || edges > 256
        || capacity < 1 || capacity > 256 || c->df
        || ((c->r[2] | X_M32(sp+8) | X_M32(sp+16)) & 3u)
        || c->preempt <= edges*257+1) {
        ++xp_fallback_calls;
        __real_f_000B7F10(c);
        return;
    }
    ++xp_typed_calls;
    test_candidate(c);
#ifdef XP_TEST_ZERO_COUNT
    c->r[0] = 0;
#endif
#ifdef XP_TEST_SHIFT_OUTPUT
    for (int i = 0; i < (int16_t)c->r[0]; ++i) {
        uint32_t p = X_M32(sp+16)+i*8;
        x87_store_f32(c,p,x87_load_f32(c,p)+4.0);
    }
#endif
    c->r[4] += 24;
}
#endif
