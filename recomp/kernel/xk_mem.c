/* xk_mem.c - guest memory model + Mm/Ex/Nt memory exports.
 *
 * Findings from booting Halo (see architecture addendum "physical vs virtual"): the game asks the kernel for
 * physical memory at fixed addresses (its 22 MB tag cache at physical 0x3A6000, referenced through
 * 0x803A6000 pointers baked into the .map files) while the XBE image occupies the same numbers as VIRTUAL
 * addresses.  So guest addresses go through a 4 KB page table:
 *
 *   arena = [0, 64 MB) physical RAM  |  [64 MB, 64 MB + image) XBE image copy  |  trash page
 *   virtual page -> arena offset; 0x80000000-0x83FFFFFF and 0xF0000000-0xF3FFFFFF alias physical 1:1;
 *   virtual [0, 64 MB) is identity-mapped by default, except the image, and except pages that
 *   NtAllocateVirtualMemory backs with other physical pages.
 */
#include <stdlib.h>
#include <string.h>
#include "xk.h"

#define XRAM_SIZE      (64u << 20)
#define NPAGES         (XRAM_SIZE / XK_PAGE)          /* physical pages */
#define KERNEL_VA      0x03D00000u                    /* [KERNEL_VA, 64 MB): identity-mapped kernel objects */
#define MAX_RANGES     2048

uint32_t *g_xpt;                                      /* 1M entries: virtual page -> arena byte offset */
static uint8_t  g_phys_used[NPAGES];                  /* physical page bitmap (byte per page) */
static uint8_t  g_virt_committed[NPAGES];             /* virtual pages below 64 MB that own a private physical page */
static uint32_t g_image_lo, g_image_hi, g_trash_off;
/* 64-byte kernel-pool units: zero = free, length at the head, UINT16_MAX inside.
 * 96 KB of host metadata covers the entire 3 MB pool, with no fixed free-list limit. */
#define KPOOL_UNIT 64u
#define KPOOL_UNITS ((XRAM_SIZE - KERNEL_VA) / KPOOL_UNIT)
static uint16_t g_kunits[KPOOL_UNITS];
static uint32_t g_khint;
uint8_t *g_img_base;   /* g_xram + XRAM_SIZE - g_image_lo: flat base for constant image-address access (xv_x86rt.h X_IMG*) */

typedef struct { uint32_t va, size; uint32_t flags; } vrange_t;       /* virtual reservations */
static vrange_t g_vr[MAX_RANGES]; static int g_nvr;
typedef struct { uint32_t pa, size; } prange_t;                       /* physical allocations (for size queries/free) */
static prange_t g_pr[MAX_RANGES]; static int g_npr;

static inline void map_page(uint32_t va, uint32_t arena_off) { g_xpt[va >> 12] = arena_off; }

void xk_mem_init(uint32_t image_end)
{
    (void)image_end;
}

/* called by the runtime with the image location (virtual) before anything else */
void xk_mem_setup(uint32_t image_base, uint32_t image_size)
{
    g_image_lo = image_base & ~(XK_PAGE - 1);
    g_image_hi = (image_base + image_size + XK_PAGE - 1) & ~(XK_PAGE - 1);
    g_trash_off = XRAM_SIZE + (g_image_hi - g_image_lo);
    g_xpt = malloc((1u << 20) * sizeof(uint32_t));
    for (uint32_t p = 0; p < (1u << 20); ++p) g_xpt[p] = g_trash_off;
    for (uint32_t va = 0; va < XRAM_SIZE; va += XK_PAGE) {
        map_page(va, va);                                         /* identity by default */
        map_page(0x80000000u + va, va);                           /* MmGetPhysicalAddress|0x80000000 alias */
        map_page(0xF0000000u + va, va);                           /* GPU/write-combined alias */
    }
    for (uint32_t va = g_image_lo; va < g_image_hi; va += XK_PAGE) map_page(va, XRAM_SIZE + (va - g_image_lo));
    g_img_base = 0;   /* set once g_xram exists (xk_mem_bind_arena); until then X_IMG must not be used */
    memset(g_phys_used, 0, sizeof g_phys_used);
    for (uint32_t pa = KERNEL_VA; pa < XRAM_SIZE; pa += XK_PAGE) g_phys_used[pa / XK_PAGE] = 1;   /* kernel area: identity, reserved */
    g_nvr = 0; g_npr = 0;
    memset(g_kunits, 0, sizeof g_kunits); g_khint = 0;
    /* the image's virtual range is reserved (never handed out) */
    g_vr[g_nvr++] = (vrange_t){ g_image_lo, g_image_hi - g_image_lo, 0xFFFFFFFFu };
    g_vr[g_nvr++] = (vrange_t){ KERNEL_VA, XRAM_SIZE - KERNEL_VA, 0xFFFFFFFFu };
}
uint32_t xk_mem_image_arena_offset(void) { return XRAM_SIZE; }
extern uint8_t *g_xram;
/* Call after g_xram is allocated: fixes the flat base for constant image-address access (X_IMG*).
 * g_img_base + a == X_G(a) for every a in [g_image_lo, g_image_hi) (the image is mapped at arena
 * offset XRAM_SIZE and never remapped). */
void xk_mem_bind_arena(void) { g_img_base = g_xram + XRAM_SIZE - g_image_lo; }
uint32_t xk_mem_image_lo(void) { return g_image_lo; }
uint32_t xk_mem_image_hi(void) { return g_image_hi; }
uint32_t xk_mem_arena_size(void) { return g_trash_off + XK_PAGE; }

/* ---- physical --------------------------------------------------------------------------------- */
static int phys_range_free(uint32_t pa, uint32_t size) { for (uint32_t p = pa / XK_PAGE, n = size / XK_PAGE; n; --n, ++p) if (p >= NPAGES || g_phys_used[p]) return 0; return 1; }
static void phys_mark(uint32_t pa, uint32_t size, int used) { for (uint32_t p = pa / XK_PAGE, n = size / XK_PAGE; n; --n, ++p) g_phys_used[p] = (uint8_t)used; }

uint32_t xk_phys_alloc(uint32_t size, uint32_t align, uint32_t lowest, uint32_t highest, int top_down)
{
    size = (size + XK_PAGE - 1) & ~(XK_PAGE - 1);
    if (align < XK_PAGE) align = XK_PAGE;
    if (!size) return 0;
    if (highest == 0 || highest == 0xFFFFFFFFu || highest >= KERNEL_VA) highest = KERNEL_VA - 1;
    lowest &= 0x03FFFFFFu; highest &= 0x03FFFFFFu;
    uint32_t first = (lowest + align - 1) & ~(align - 1);
    if (highest + 1 < size) return 0;
    uint32_t last = ((highest + 1 - size) & ~(align - 1));
    if (first > last) return 0;
    if (top_down) { for (uint32_t a = last; ; a -= align) { if (phys_range_free(a, size)) { phys_mark(a, size, 1); memset(g_xram + a, 0, size); if (g_npr < MAX_RANGES) g_pr[g_npr++] = (prange_t){ a, size }; return a; } if (a < first + align) break; } }
    else { for (uint32_t a = first; a <= last; a += align) if (phys_range_free(a, size)) { phys_mark(a, size, 1); memset(g_xram + a, 0, size); if (g_npr < MAX_RANGES) g_pr[g_npr++] = (prange_t){ a, size }; return a; } }
    return 0;
}
int xk_phys_free(uint32_t pa)
{
    pa &= 0x03FFFFFFu;
    for (int i = 0; i < g_npr; ++i) if (g_pr[i].pa == pa) { phys_mark(pa, g_pr[i].size, 0); g_pr[i] = g_pr[--g_npr]; return 0; }
    return -1;
}
static uint32_t phys_size(uint32_t pa) { pa &= 0x03FFFFFFu; for (int i = 0; i < g_npr; ++i) if (g_pr[i].pa == pa) return g_pr[i].size; return 0; }
static uint32_t phys_free_pages(void) { uint32_t n = 0; for (uint32_t p = 0; p < NPAGES; ++p) if (!g_phys_used[p]) n++; return n; }

/* ---- virtual --------------------------------------------------------------------------------------- */
static int vr_find(uint32_t va) { for (int i = 0; i < g_nvr; ++i) if (g_vr[i].va <= va && va < g_vr[i].va + g_vr[i].size) return i; return -1; }
static int vr_range_free(uint32_t va, uint32_t size) { for (int i = 0; i < g_nvr; ++i) if (va < g_vr[i].va + g_vr[i].size && g_vr[i].va < va + size) return 0; return va + size <= XRAM_SIZE && va >= 0x10000; }

static uint32_t virt_reserve(uint32_t size, uint32_t hint)
{
    size = (size + XK_PAGE - 1) & ~(XK_PAGE - 1);
    if (hint) { hint &= ~(XK_PAGE - 1); if (vr_range_free(hint, size)) goto ok; return 0; }
    /* first fit, 64 KB granularity, above the image */
    for (hint = g_image_hi; hint + size <= KERNEL_VA; hint += 0x10000) if (vr_range_free(hint, size)) goto ok;
    return 0;
ok:
    if (g_nvr >= MAX_RANGES) return 0;
    g_vr[g_nvr++] = (vrange_t){ hint, size, 0 };
    return hint;
}
/* back [va, va+size) with private physical pages (top-down, so the game's fixed physical ranges stay free) */
static int virt_commit(uint32_t va, uint32_t size)
{
    /* Back the whole commit with ONE ascending run of arena pages when possible.  The per-page path below
     * hands out pages top-down, so consecutive virtual pages (heap, thread stacks) landed in DESCENDING
     * arena pages: any block crossing 4 KB was scrambled by host-contiguous copies (rep movs fast path,
     * HLE bulk readers) - e.g. the portal clipper's in-place polygon copy came back empty and whole map
     * clusters were culled.  With ascending runs, virtual order == arena order inside a commit. */
    {
        uint32_t need = 0;
        for (uint32_t p = va; p < va + size; p += XK_PAGE) if (!g_virt_committed[p / XK_PAGE]) need++;
        if (need) {
            uint32_t bytes = need * XK_PAGE, a = 0;
            for (uint32_t cand = (KERNEL_VA - bytes) & ~(XK_PAGE - 1); ; cand -= XK_PAGE) {
                if (phys_range_free(cand, bytes)) { a = cand; break; }
                if (cand < XK_PAGE) break;
            }
            if (a) {
                uint32_t q = a;
                for (uint32_t p = va; p < va + size; p += XK_PAGE) {
                    if (g_virt_committed[p / XK_PAGE]) continue;
                    g_phys_used[q / XK_PAGE] = 1; g_virt_committed[p / XK_PAGE] = 1;
                    memset(g_xram + q, 0, XK_PAGE); map_page(p, q); q += XK_PAGE;
                }
                return 0;
            }
        }
    }
    for (uint32_t p = va; p < va + size; p += XK_PAGE) {
        if (g_virt_committed[p / XK_PAGE]) continue;
        uint32_t pa = 0;
        static uint32_t hint = KERNEL_VA / XK_PAGE;
        for (uint32_t q = hint; q-- > 0; ) if (!g_phys_used[q]) { pa = q * XK_PAGE; hint = q; break; }
        if (!pa) { for (uint32_t q = KERNEL_VA / XK_PAGE; q-- > 0; ) if (!g_phys_used[q]) { pa = q * XK_PAGE; break; } }
        if (!pa) return -1;
        g_phys_used[pa / XK_PAGE] = 1; g_virt_committed[p / XK_PAGE] = 1;
        memset(g_xram + pa, 0, XK_PAGE); map_page(p, pa);
    }
    return 0;
}
static void virt_release(uint32_t va, uint32_t size)
{
    for (uint32_t p = va; p < va + size; p += XK_PAGE) {
        if (g_virt_committed[p / XK_PAGE]) { uint32_t off = g_xpt[p >> 12]; if (off < XRAM_SIZE) g_phys_used[off / XK_PAGE] = 0; g_virt_committed[p / XK_PAGE] = 0; }
        map_page(p, p);
    }
}

/* generic helpers used by the kernel itself (stacks, TLS) */
uint32_t xk_mem_alloc(uint32_t size, uint32_t align, uint32_t lowest, uint32_t highest, int top_down)
{
    (void)align; (void)lowest; (void)highest; (void)top_down;
    uint32_t va = virt_reserve(size, 0);
    if (!va) return 0;
    if (virt_commit(va, (size + XK_PAGE - 1) & ~(XK_PAGE - 1)) != 0) { xk_mem_free(va); return 0; }
    return va;
}
int xk_mem_free(uint32_t va)
{
    int i = vr_find(va);
    if (i < 0 || g_vr[i].flags == 0xFFFFFFFFu) return -1;
    virt_release(g_vr[i].va, g_vr[i].size); g_vr[i] = g_vr[--g_nvr]; return 0;
}
uint32_t xk_mem_size(uint32_t va) { int i = vr_find(va); return i >= 0 ? g_vr[i].size : 0; }
uint32_t xk_mem_available(void) { return phys_free_pages() * XK_PAGE; }

/* Kernel objects remain identity-mapped; adjacent freed units can serve larger requests. */
uint32_t xk_kalloc(uint32_t size)
{
    if (!size) size = 1;
    if (size > XRAM_SIZE - KERNEL_VA) return 0;
    uint32_t need = (size + KPOOL_UNIT - 1) / KPOOL_UNIT;
    uint32_t run = 0;
    for (uint32_t i = g_khint; i < KPOOL_UNITS; ) {
        if (g_kunits[i]) { run = 0; i += g_kunits[i] == UINT16_MAX ? 1 : g_kunits[i]; continue; }
        ++i;
        if (++run < need) continue;
        uint32_t start = i - need;
        g_kunits[start] = (uint16_t)need;
        for (uint32_t j = start + 1; j < i; ++j) g_kunits[j] = UINT16_MAX;
        /* Preserve earlier holes that were too small for this request. */
        if (start == g_khint) g_khint = i;
        uint32_t a = KERNEL_VA + start * KPOOL_UNIT;
        memset(X_G(a), 0, need * KPOOL_UNIT);
        return a;
    }
    XK_LOG("kalloc: out of kernel memory\n"); return 0;
}
void xk_kfree(uint32_t addr)
{
    if (addr < KERNEL_VA || addr >= XRAM_SIZE || (addr & (KPOOL_UNIT - 1))) return;
    uint32_t start = (addr - KERNEL_VA) / KPOOL_UNIT;
    uint16_t n = g_kunits[start];
    if (!n || n == UINT16_MAX) return; /* null, duplicate or interior free */
    memset(&g_kunits[start], 0, n * sizeof g_kunits[0]);
    if (start < g_khint) g_khint = start;
}

/* ---- exports ------------------------------------------------------------------------------------ */
/* PVOID MmAllocateContiguousMemoryEx(NumberOfBytes, LowestAcceptable, HighestAcceptable, Alignment, Protect) */
void xk_MmAllocateContiguousMemoryEx(xctx *c)
{
    uint32_t n = X_ARG(0), lo = X_ARG(1), hi = X_ARG(2), al = X_ARG(3);
    uint32_t a = xk_phys_alloc(n, al, lo, hi, 1);
    XK_LOG("MmAllocateContiguousMemoryEx(%u KB, %08X..%08X, align %X) -> %08X\n", n >> 10, lo, hi, al, a ? 0x80000000u | a : 0);
    c->r[0] = a ? 0x80000000u | a : 0; X_RET(5);
}
void xk_MmAllocateContiguousMemory(xctx *c) { uint32_t a = xk_phys_alloc(X_ARG(0), 0, 0, 0, 1); XK_LOG("MmAllocateContiguousMemory(%u KB) -> %08X\n", X_ARG(0) >> 10, a ? 0x80000000u | a : 0); c->r[0] = a ? 0x80000000u | a : 0; X_RET(1); }
void xk_MmFreeContiguousMemory(xctx *c) { xk_phys_free(X_ARG(0)); X_RET(1); }
void xk_MmAllocateSystemMemory(xctx *c) { uint32_t a = xk_mem_alloc(X_ARG(0), 0, 0, 0, 0); c->r[0] = a; X_RET(2); }
void xk_MmFreeSystemMemory(xctx *c) { xk_mem_free(X_ARG(0)); c->r[0] = X_ARG(1); X_RET(2); }
void xk_MmQueryAllocationSize(xctx *c) { uint32_t a = X_ARG(0); c->r[0] = (a & 0x80000000u) ? phys_size(a) : xk_mem_size(a); X_RET(1); }
void xk_MmGetPhysicalAddress(xctx *c) { uint32_t va = X_ARG(0); uint32_t off = g_xpt[va >> 12]; c->r[0] = (off < XRAM_SIZE ? off : (va & 0x03FFFFFFu)) | (va & 0xFFF); X_RET(1); }
void xk_MmPersistContiguousMemory(xctx *c) { X_RET(3); }
void xk_MmLockUnlockBufferPages(xctx *c) { X_RET(3); }
void xk_MmLockUnlockPhysicalPage(xctx *c) { X_RET(2); }
void xk_MmSetAddressProtect(xctx *c) { X_RET(3); }
void xk_MmQueryAddressProtect(xctx *c) { c->r[0] = 0x04; X_RET(1); }
void xk_MmIsAddressValid(xctx *c) { c->r[0] = g_xpt[X_ARG(0) >> 12] != g_trash_off; X_RET(1); }
void xk_MmMapIoSpace(xctx *c) { c->r[0] = X_ARG(0) | 0x80000000u; X_RET(3); }
void xk_MmUnmapIoSpace(xctx *c) { X_RET(2); }
void xk_MmClaimGpuInstanceMemory(xctx *c) { c->r[0] = 0x83FF0000u; X_RET(2); }
void xk_MmQueryStatistics(xctx *c)
{
    uint32_t s = X_ARG(0);
    if (X_M32(s) >= 36) {
        X_M32(s + 4) = NPAGES; X_M32(s + 8) = phys_free_pages(); X_M32(s + 12) = (NPAGES - phys_free_pages()) * XK_PAGE;
        X_M32(s + 16) = 0; X_M32(s + 20) = 0; X_M32(s + 24) = 0; X_M32(s + 28) = 0; X_M32(s + 32) = (g_image_hi - g_image_lo) / XK_PAGE;
        c->r[0] = STATUS_SUCCESS;
    } else c->r[0] = STATUS_INVALID_PARAMETER;
    X_RET(1);
}
/* NTSTATUS NtAllocateVirtualMemory(PVOID *BaseAddress, ULONG ZeroBits, PULONG RegionSize, ULONG AllocationType, ULONG Protect) */
void xk_NtAllocateVirtualMemory(xctx *c)
{
    uint32_t pbase = X_ARG(0), psize = X_ARG(2), type = X_ARG(3);
    uint32_t base = X_M32(pbase), size = X_M32(psize);
    uint32_t va;
    if (base) {                                            /* commit (or re-reserve) inside an existing reservation */
        uint32_t lo = base & ~(XK_PAGE - 1), hi = (base + size + XK_PAGE - 1) & ~(XK_PAGE - 1);
        int i = vr_find(lo);
        if (i >= 0 && g_vr[i].flags != 0xFFFFFFFFu) { va = lo; }
        else { va = virt_reserve(hi - lo, lo); if (!va) { XK_LOG("NtAllocateVirtualMemory: hint %08X busy\n", base); va = virt_reserve(hi - lo, 0); } }
        if (!va) { c->r[0] = STATUS_NO_MEMORY; X_RET(5); }
        if ((type & 0x1000) && virt_commit(va, hi - lo) != 0) { c->r[0] = STATUS_NO_MEMORY; X_RET(5); }
        X_M32(pbase) = va; X_M32(psize) = hi - lo;
    } else {
        va = virt_reserve(size, 0);
        if (!va) { XK_LOG("NtAllocateVirtualMemory: out of address space (%u KB)\n", size >> 10); c->r[0] = STATUS_NO_MEMORY; X_RET(5); }
        size = (size + XK_PAGE - 1) & ~(XK_PAGE - 1);
        if ((type & 0x1000) && virt_commit(va, size) != 0) { c->r[0] = STATUS_NO_MEMORY; X_RET(5); }
        X_M32(pbase) = va; X_M32(psize) = size;
    }
    XK_LOG("NtAllocateVirtualMemory(base %08X, %u KB, type %X) -> %08X\n", base, size >> 10, type, va);
    c->r[0] = STATUS_SUCCESS; X_RET(5);
}
void xk_NtFreeVirtualMemory(xctx *c)
{
    uint32_t base = X_M32(X_ARG(0)) & ~(XK_PAGE - 1), type = X_ARG(2);
    if (type & 0x8000) xk_mem_free(base);                                            /* MEM_RELEASE */
    else if (type & 0x4000) { uint32_t n = (X_M32(X_ARG(1)) + XK_PAGE - 1) & ~(XK_PAGE - 1); virt_release(base, n); }   /* MEM_DECOMMIT */
    c->r[0] = STATUS_SUCCESS; X_RET(3);
}
void xk_NtQueryVirtualMemory(xctx *c)
{
    uint32_t va = X_ARG(0), info = X_ARG(1); int i = vr_find(va);
    /* MEMORY_BASIC_INFORMATION { BaseAddress, AllocationBase, AllocationProtect, RegionSize, State, Protect, Type } */
    X_M32(info) = va & ~(XK_PAGE - 1); X_M32(info + 4) = i >= 0 ? g_vr[i].va : 0; X_M32(info + 8) = 4;
    X_M32(info + 12) = i >= 0 ? g_vr[i].size : XK_PAGE; X_M32(info + 16) = i >= 0 ? 0x1000 : 0x10000; X_M32(info + 20) = 4; X_M32(info + 24) = 0x20000;
    c->r[0] = STATUS_SUCCESS; X_RET(3);
}
void xk_NtProtectVirtualMemory(xctx *c) { if (X_ARG(3)) X_M32(X_ARG(3)) = 4; c->r[0] = STATUS_SUCCESS; X_RET(4); }
void xk_ExAllocatePoolWithTag(xctx *c) { uint32_t n = X_ARG(0); c->r[0] = n >= XK_PAGE ? xk_mem_alloc(n, 0, 0, 0, 0) : xk_kalloc(n); X_RET(2); }
void xk_ExAllocatePool(xctx *c) { uint32_t n = X_ARG(0); c->r[0] = n >= XK_PAGE ? xk_mem_alloc(n, 0, 0, 0, 0) : xk_kalloc(n); X_RET(1); }
void xk_ExFreePool(xctx *c) { if (X_ARG(0) < KERNEL_VA) xk_mem_free(X_ARG(0)); else xk_kfree(X_ARG(0)); X_RET(1); }
void xk_ExQueryPoolBlockSize(xctx *c) { c->r[0] = X_ARG(0) < KERNEL_VA ? xk_mem_size(X_ARG(0)) : 64; X_RET(1); }
