#include "xk_query_capture.h"

static int active(const XvQueryMemory *cap)
{ return cap && cap->status == XV_QM_RECORDING; }

void xv_query_capture_abandon(XvQueryMemory *cap, unsigned reason)
{
    if (active(cap)) xv_query_memory_invalidate(cap, reason);
}

static int roots(XvQueryMemory *cap, const uint8_t *arena, const uint32_t *pages)
{
    if (arena != cap->view.arena || pages != cap->view.pages) {
        xv_query_capture_abandon(cap, XV_QM_ROOTS);
        return 0;
    }
    return 1;
}

void xv_query_capture_physical(XvQueryMemory *cap, const void *actual_pointer,
    uint32_t size, unsigned write)
{
    uintptr_t base, pointer, offset;
    if (!active(cap) || !size) return;
    base = (uintptr_t)cap->view.arena;
    pointer = (uintptr_t)actual_pointer;
    if (pointer < base) {
        xv_query_capture_abandon(cap, XV_QM_BOUNDS);
        return;
    }
    offset = pointer - base;
    if (offset > cap->view.usable_size || size > cap->view.usable_size - offset) {
        xv_query_capture_abandon(cap, XV_QM_BOUNDS);
        return;
    }
    if (write) xv_query_memory_write(cap, (uint32_t)offset, size);
    else xv_query_memory_read(cap, (uint32_t)offset, size);
}

void xv_query_capture_touch(XvQueryMemory *cap, const uint8_t *arena,
    const uint32_t *pages, uint32_t address, const void *actual_pointer,
    uint32_t size, unsigned write)
{
    uint32_t page, physical;
    uintptr_t base, offset;
    if (!active(cap) || !size || !roots(cap, arena, pages)) return;
    page = address >> 12;
    if (page >= cap->view.page_count || size - 1u > UINT32_MAX - address) {
        xv_query_capture_abandon(cap, XV_QM_BOUNDS);
        return;
    }
    physical = pages[page];
    if (!xv_query_memory_mapping(cap, page, physical)) return;
    /* mapping admitted a full usable physical page, so this sum is bounded
     * by usable_size. Compare integer addresses before touching the span. */
    offset = (uintptr_t)physical + (address & 4095u);
    base = (uintptr_t)arena;
    if ((uintptr_t)actual_pointer != base + offset) {
        xv_query_capture_abandon(cap, XV_QM_BOUNDS);
        return;
    }
    xv_query_capture_physical(cap, actual_pointer, size, write);
}

uint32_t xv_query_capture_pte(XvQueryMemory *cap, const uint8_t *arena,
    const uint32_t *pages, uint32_t page)
{
    uint32_t value = pages[page];
    if (active(cap) && roots(cap, arena, pages)) {
        if (page >= cap->view.page_count) xv_query_capture_abandon(cap, XV_QM_BOUNDS);
        else xv_query_memory_mapping(cap, page, value);
    }
    return value;
}
