/* Exact-value reuse for the already validated Halo 3925 quaternion helper.
 * No guest addresses survive a call. Keys include all inputs, constants and
 * native FP control bits; hits reproduce guest state and native FP status.
 * Opt-in build/runtime experiment, owned by the serialized guest thread. */
#ifdef XV_QUAT_CACHE
#include "xk.h"
#include "xk_quat_cache.h"
#include <stdlib.h>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

#define CACHE_SLOTS 64u
typedef struct {
    uint32_t key[8], fp_status, condition;
    float output[13], scratch[6];
    double st[6];
    unsigned valid;
} quat_entry;
static quat_entry entries[CACHE_SLOTS];
static unsigned hits, misses, disabled, unsupported;

#if defined(__arm__)
#define FP_STICKY 0x0000009Fu
#define FP_CONDITION 0xF0000000u
static uint32_t fp_get(void) { uint32_t value; __asm__ volatile("vmrs %0, fpscr" : "=r"(value) :: "memory"); return value; }
static void fp_set(uint32_t value) { __asm__ volatile("vmsr fpscr, %0" :: "r"(value) : "memory"); }
static int fp_supported(uint32_t value) { return !(value & 0x00009F00u); }
#elif defined(__x86_64__)
/* Host equivalence tests use the compiler's scalar SSE math. */
#define FP_STICKY 0x0000003Fu
#define FP_CONDITION 0u
static uint32_t fp_get(void) { return _mm_getcsr(); }
static void fp_set(uint32_t value) { _mm_setcsr(value); }
static int fp_supported(uint32_t value) { return (value & 0x1F80u) == 0x1F80u; }
#else
#define FP_STICKY 0u
#define FP_CONDITION 0u
static uint32_t fp_get(void) { return 0; }
static void fp_set(uint32_t value) { (void)value; }
static int fp_supported(uint32_t value) { (void)value; return 0; }
#endif

static int enabled(void)
{
    static int setting = -1;
    if (setting < 0) {
        const char *value = getenv("XV_QUAT_CACHE");
        setting = value && atoi(value) != 0;
    }
    return setting;
}

int xv_quat_cache_restore(xctx *c, const float *input, float *output,
                          float *scratch, const void *constants,
                          xv_quat_cache_request *request)
{
    request->active = 0;
    if (!enabled()) { disabled++; return 0; }
    uint32_t before = fp_get();
    if (!fp_supported(before)) { unsupported++; return 0; }
    memcpy(request->key, input, 16);
    const uint8_t *constant = constants;
    memcpy(&request->key[4], constant, 4);        /* 0x1F0A68 */
    memcpy(&request->key[5], constant + 0x10, 4); /* 0x1F0A78 */
    memcpy(&request->key[6], constant + 0x9C, 4); /* 0x1F0B04 */
    request->key[7] = before & ~(FP_STICKY | FP_CONDITION);
    /* Constants/control still participate in exact equality. Hash only the
     * quaternion to keep the lookup cheaper than the operation it replaces. */
    uint32_t hash = request->key[0] ^ (request->key[1] >> 8) ^
                    (request->key[2] >> 16) ^ request->key[3] ^
                    (request->key[3] >> 7);
    hash ^= hash >> 16;
    request->slot = (hash * 0x9E3779B1u) >> 26;
    quat_entry *entry = &entries[request->slot];
    request->fp_before = before;
    if (entry->valid && !memcmp(entry->key, request->key, sizeof entry->key)) {
        memcpy(output, entry->output, sizeof entry->output);
        memcpy(scratch, entry->scratch, sizeof entry->scratch);
        unsigned fp = c->fsp;
#define RESTORE_ST(i) memcpy(&c->st[(fp+(i)+2)&7u], &entry->st[i], sizeof(double))
        RESTORE_ST(0); RESTORE_ST(1); RESTORE_ST(2);
        RESTORE_ST(3); RESTORE_ST(4); RESTORE_ST(5);
#undef RESTORE_ST
        c->fsw = (uint16_t)((c->fsw & ~0x4700u) | entry->condition |
                            (((c->fsp+7u)&7u) << 11));
        X_R16(0) = c->fsw;
        uint8_t result = X_R8H(0) & 0x44u;
        X_FLAGS(XK_LOGIC,0,0,result,8);
        c->r[0] = 0;
        fp_set((before & ~FP_CONDITION) | entry->fp_status);
        hits++;
        return 1;
    }
    request->active = 1;
    /* Capture the operation's raised flags even if they were already set on
     * entry, then merge the caller's sticky flags back after the miss. */
    fp_set(before & ~FP_STICKY);
    misses++;
    return 0;
}

void xv_quat_cache_store(const xctx *c, const float *output, const float *scratch,
                         const xv_quat_cache_request *request)
{
    if (!request->active) return;
    uint32_t after = fp_get();
    quat_entry *entry = &entries[request->slot];
    memcpy(entry->key, request->key, sizeof entry->key);
    memcpy(entry->output, output, sizeof entry->output);
    memcpy(entry->scratch, scratch, sizeof entry->scratch);
    unsigned fp = c->fsp;
#define SAVE_ST(i) memcpy(&entry->st[i], &c->st[(fp+(i)+2)&7u], sizeof(double))
    SAVE_ST(0); SAVE_ST(1); SAVE_ST(2);
    SAVE_ST(3); SAVE_ST(4); SAVE_ST(5);
#undef SAVE_ST
    entry->condition = c->fsw & 0x4500u;
    entry->fp_status = after & (FP_STICKY | FP_CONDITION);
    entry->valid = 1;
    fp_set(after | (request->fp_before & FP_STICKY));
}

void xv_quat_cache_report(unsigned frames)
{
    XK_LOG("[quat-cache] %u frames hits %u misses %u disabled %u unsupported-fp %u\n",
           frames,hits,misses,disabled,unsupported);
    hits=misses=disabled=unsupported=0;
}
#endif
