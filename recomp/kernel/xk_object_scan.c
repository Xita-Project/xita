/* Optional Halo 3925 empty-object scan at 0x90190/0x90240/0x902B6.
 * Only zero identifiers are batched. Active entries, callbacks, the exit
 * iteration and any scheduler handoff remain in the original translated loop.
 * The game profile validates the complete containing function before hooking.
 */
#ifdef XV_NATIVE_OBJECT_SCAN
#include "xk.h"
#include <stdlib.h>

/* Guest-owner flag lets disabled generated loops avoid a helper call. The
 * initial sentinel resolves the environment on the first eligible entry. */
int xv_object_scan_active = -1;
static unsigned scan_calls[3], scan_entries[3];
static int scan_configured(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("XV_NATIVE_OBJECT_SCAN");
        enabled = value && atoi(value) != 0;
    }
    return enabled;
}
void xv_object_scan_override(int value)
{
    xv_object_scan_active = value < 0 ? scan_configured() : !!value;
}

unsigned xv_object_scan_empty(xctx *c, unsigned pass)
{
    if (pass > 2) return 0;
    if (xv_object_scan_active < 0) xv_object_scan_active = scan_configured();
    if (!xv_object_scan_active) return 0;
    scan_calls[pass]++;
    /* Signed 16-bit loop indices are normally nonnegative. Preserve the
     * original wrap/negative behavior by declining those unusual inputs. */
    uint32_t index = c->r[7], address = c->r[6];
    if (index > 32766u || c->preempt <= 1) return 0;
    unsigned bytes = 4096u - (address & 4095u);
    if (bytes < 2u) return 0;
    const uint8_t *records = X_G(address);
    uint16_t first;
    memcpy(&first, records, sizeof first);
    if (first) return 0;
    uint32_t table = pass ? c->r[5] : X_IMG32(0x2FC6ACu);
    if (table > UINT32_MAX - 0x30u) return 0;
    uint32_t limit_address = table + 0x2eu;
    if ((limit_address & 4095u) == 4095u) return 0;
    int limit = (int16_t)X_M16(limit_address);
    if (limit <= 0 || index + 1u >= (unsigned)limit) return 0;
    /* Leave the final iteration and a possible yield to the original loop.
     * Restrict each batch to one translated page and at most 128 records. */
    unsigned count = (unsigned)limit - index - 1u;
    unsigned budget = (unsigned)c->preempt - 1u;
    if (count > budget) count = budget;
    if (count > 128u) count = 128u;
    unsigned page_count = (bytes - 2u) / 12u + 1u;
    if (count > page_count) count = page_count;
    if (count < 2u) return 0;
    unsigned skipped = 1;
    while (skipped < count) {
        uint16_t identifier;
        memcpy(&identifier, records + skipped * 12u, sizeof identifier);
        if (identifier) break;
        skipped++;
    }
    if (skipped < 2u) return 0;
    /* Reproduce the state after the last skipped back edge. The zero test
     * clears carry before INC preserves it. The final CMP owns lazy flags. */
    if (pass == 0) c->r[0] = table;
    else if (pass == 1) X_R16(0) = 0;
    else X_R16(1) = 0;
    c->r[6] += skipped * 12u;
    c->r[7] += skipped;
    c->preempt -= (int32_t)skipped;
    c->f_cf = 0;
    uint32_t current = c->r[7];
    X_FLAGS(XK_SUB, current, (uint32_t)limit,
            (uint16_t)(current - (uint32_t)limit), 16);
    scan_entries[pass] += skipped;
    return skipped;
}

void xv_object_scan_report(unsigned frames)
{
    XK_LOG("[object-scan] %u frames calls %u/%u/%u empty entries %u/%u/%u\n",
           frames, scan_calls[0], scan_calls[1], scan_calls[2],
           scan_entries[0], scan_entries[1], scan_entries[2]);
    memset(scan_calls, 0, sizeof scan_calls);
    memset(scan_entries, 0, sizeof scan_entries);
}
#endif
