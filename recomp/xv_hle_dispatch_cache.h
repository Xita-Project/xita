/* Optional cache of immutable HLE function-table resolutions. The serialized
 * guest owner is the only caller; no guest data pointers are retained. */
#ifndef XV_HLE_DISPATCH_CACHE_H
#define XV_HLE_DISPATCH_CACHE_H

static struct { uint32_t target; xv_fn_t fn; } hle_dispatch_cache[256];
static unsigned hle_dispatch_lookups, hle_dispatch_hits, hle_dispatch_stores;
static unsigned hle_dispatch_guest_calls, hle_dispatch_hle_calls;
static int hle_dispatch_override = -1;

void xv_hle_dispatch_override(int value)
{ hle_dispatch_override = value < 0 ? -1 : !!value; }

static int hle_dispatch_enabled(void)
{
    static int configured = -1;
    if (configured < 0) {
        const char *value = getenv("XV_HLE_DISPATCH_CACHE");
        configured = value && atoi(value) != 0;
    }
    return hle_dispatch_override < 0 ? configured : hle_dispatch_override;
}

static xv_fn_t hle_dispatch_find(uint32_t target)
{
    if (!hle_dispatch_enabled()) return NULL;
    unsigned slot = (target >> 2) & 255u;
    hle_dispatch_lookups++;
    if (hle_dispatch_cache[slot].target != target || !hle_dispatch_cache[slot].fn)
        return NULL;
    hle_dispatch_hits++;
    return hle_dispatch_cache[slot].fn;
}

static void hle_dispatch_store(uint32_t target, xv_fn_t fn)
{
    if (!hle_dispatch_enabled()) return;
    unsigned slot = (target >> 2) & 255u;
    hle_dispatch_cache[slot].target = target;
    hle_dispatch_cache[slot].fn = fn;
    hle_dispatch_stores++;
}

void xv_hle_dispatch_report(unsigned frames)
{
    extern void xv_logf(const char *, ...) __attribute__((weak));
    /* Firmware printf alone does not reach the remotely collected save log. */
    if (xv_logf)
        xv_logf("[hle-dispatch] %u frames guest %u HLE %u lookups %u hits %u stores %u\n",
                frames, hle_dispatch_guest_calls, hle_dispatch_hle_calls,
                hle_dispatch_lookups, hle_dispatch_hits, hle_dispatch_stores);
    else XV_RT_LOG("[hle-dispatch] %u frames guest %u HLE %u lookups %u hits %u stores %u\n",
              frames, hle_dispatch_guest_calls, hle_dispatch_hle_calls,
              hle_dispatch_lookups, hle_dispatch_hits, hle_dispatch_stores);
    hle_dispatch_lookups = hle_dispatch_hits = hle_dispatch_stores = 0;
    hle_dispatch_guest_calls = hle_dispatch_hle_calls = 0;
}
#endif
