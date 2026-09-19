#ifndef XK_QUERY_CAPTURE_H
#define XK_QUERY_CAPTURE_H
#include "xk_query_memory.h"

/* Explicit recording context only: no global/TLS state, guest access execution,
 * fault policy, allocation, synchronization or callbacks. NULL and non-recording
 * contexts are ignored. The caller retains the memory recorder's lifetime and
 * serialization contract and instruments every access before it executes.
 * Store values must be evaluated before a write notification: do not put a
 * notifying function in an assignment's lvalue ahead of an RHS guest read.
 */
void xv_query_capture_abandon(XvQueryMemory *cap, unsigned reason);

/* One starting PTE and a physically contiguous access, even across a guest-page
 * boundary. Never split/retranslate the underlying operation. actual_pointer
 * is the pointer the original operation will use. Roots, translation, guest
 * address extent and physical bounds must agree with cap's usable arena (trash
 * excluded). A mismatch abandons recording, not the original access. size=0
 * records no access. write is zero for read, nonzero for write-before-store.
 */
void xv_query_capture_touch(XvQueryMemory *cap, const uint8_t *arena,
    const uint32_t *pages, uint32_t address, const void *actual_pointer,
    uint32_t size, unsigned write);

/* Execute the original pages[page] read and return it unchanged, even with NULL
 * or invalid cap, changed roots, or page outside the recording view. Therefore
 * pages[page] must be a valid original host read; this is NOT a safe guest-read
 * API. Recording checks bounds before any additional PTE read. Captures mapping
 * dependencies used only for admission/alias comparisons, without data reads.
 */
uint32_t xv_query_capture_pte(XvQueryMemory *cap, const uint8_t *arena,
    const uint32_t *pages, uint32_t page);

/* Already translated host span, bounded against cap's arena. Does not invent a
 * guest mapping dependency: caller registers each consulted PTE separately.
 * Direct-image root identity remains an additional caller admission condition.
 * size=0 records no access. Never reads/writes the original span itself.
 */
void xv_query_capture_physical(XvQueryMemory *cap, const void *actual_pointer,
    uint32_t size, unsigned write);
#endif
