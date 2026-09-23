/* xk_rtl.c - Rtl* exports: strings, critical sections (cooperative), time conversion, misc. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <wctype.h>
#include "xk.h"

const char *xk_gstr(uint32_t a)
{
    if (!a || (a & 0x03FFFFFFu) > (64u << 20) - 4096) return "";
    return (const char *)X_G(a);
}
void xk_ansi_to_c(uint32_t as, char *out, unsigned cap)
{
    unsigned len = AS_LEN(as); uint32_t buf = AS_BUF(as);
    if (!buf) { out[0] = 0; return; }
    if (len >= cap) len = cap - 1;
    memcpy(out, X_G(buf), len); out[len] = 0;
}

void xk_RtlInitAnsiString(xctx *c)
{
    uint32_t dst = X_ARG(0), src = X_ARG(1);
    AS_BUF(dst) = src;
    if (src) { uint16_t n = (uint16_t)strlen(xk_gstr(src)); AS_LEN(dst) = n; AS_MAX(dst) = n + 1; } else { AS_LEN(dst) = 0; AS_MAX(dst) = 0; }
    X_RET(2);
}
void xk_RtlInitUnicodeString(xctx *c)
{
    uint32_t dst = X_ARG(0), src = X_ARG(1);
    AS_BUF(dst) = src;
    if (src) { uint16_t n = 0; while (X_M16(src + n * 2)) n++; AS_LEN(dst) = n * 2; AS_MAX(dst) = n * 2 + 2; } else { AS_LEN(dst) = 0; AS_MAX(dst) = 0; }
    X_RET(2);
}
/* BOOLEAN RtlEqualString(PSTRING a, PSTRING b, BOOLEAN CaseInSensitive) */
void xk_RtlEqualString(xctx *c)
{
    uint32_t a = X_ARG(0), b = X_ARG(1); int ci = X_ARG(2) & 0xFF;
    int eq = AS_LEN(a) == AS_LEN(b);
    for (unsigned i = 0; eq && i < AS_LEN(a); ++i) {
        unsigned char x = X_M8(AS_BUF(a) + i), y = X_M8(AS_BUF(b) + i);
        if (ci) { x = toupper(x); y = toupper(y); }
        if (x != y) eq = 0;
    }
    c->r[0] = eq; X_RET(3);
}
void xk_RtlCompareString(xctx *c)
{
    uint32_t a = X_ARG(0), b = X_ARG(1); int ci = X_ARG(2) & 0xFF;
    unsigned n = AS_LEN(a) < AS_LEN(b) ? AS_LEN(a) : AS_LEN(b); int r = 0;
    for (unsigned i = 0; !r && i < n; ++i) {
        int x = X_M8(AS_BUF(a) + i), y = X_M8(AS_BUF(b) + i);
        if (ci) { x = toupper(x); y = toupper(y); }
        r = x - y;
    }
    c->r[0] = (uint32_t)(r ? r : (int)AS_LEN(a) - (int)AS_LEN(b)); X_RET(3);
}
void xk_RtlUpperChar(xctx *c) { c->r[0] = (uint8_t)toupper(X_ARG(0) & 0xFF); X_RET(1); }
void xk_RtlLowerChar(xctx *c) { c->r[0] = (uint8_t)tolower(X_ARG(0) & 0xFF); X_RET(1); }
void xk_RtlUpcaseUnicodeChar(xctx *c) { c->r[0] = (uint16_t)towupper(X_ARG(0) & 0xFFFF); X_RET(1); }
void xk_RtlDowncaseUnicodeChar(xctx *c) { c->r[0] = (uint16_t)towlower(X_ARG(0) & 0xFFFF); X_RET(1); }
void xk_RtlZeroMemory(xctx *c) { memset(X_GWN(X_ARG(0), X_ARG(1)), 0, X_ARG(1)); X_RET(2); }
void xk_RtlFillMemory(xctx *c) { memset(X_GWN(X_ARG(0), X_ARG(1)), (int)(X_ARG(2) & 0xFF), X_ARG(1)); X_RET(3); }
void xk_RtlMoveMemory(xctx *c) { memmove(X_GWN(X_ARG(0), X_ARG(2)), X_G(X_ARG(1)), X_ARG(2)); X_RET(3); }
void xk_RtlCompareMemory(xctx *c) { const uint8_t *a = X_G(X_ARG(0)), *b = X_G(X_ARG(1)); uint32_t n = X_ARG(2), i = 0; while (i < n && a[i] == b[i]) i++; c->r[0] = i; X_RET(3); }
void xk_RtlCompareMemoryUlong(xctx *c) { const uint32_t *a = X_G(X_ARG(0)); uint32_t n = X_ARG(1) / 4, p = X_ARG(2), i = 0; while (i < n && a[i] == p) i++; c->r[0] = i * 4; X_RET(3); }
void xk_RtlUlongByteSwap(xctx *c) { c->r[0] = __builtin_bswap32(X_ARG(0)); X_RET(1); }
void xk_RtlUshortByteSwap(xctx *c) { c->r[0] = __builtin_bswap16(X_ARG(0) & 0xFFFF); X_RET(1); }

/* NTSTATUS RtlUnicodeStringToAnsiString(PANSI_STRING dst, PUNICODE_STRING src, BOOLEAN alloc) */
void xk_RtlUnicodeStringToAnsiString(xctx *c)
{
    uint32_t dst = X_ARG(0), src = X_ARG(1); int alloc = X_ARG(2) & 0xFF;
    unsigned n = AS_LEN(src) / 2;
    if (alloc) { AS_BUF(dst) = xk_kalloc(n + 1); AS_MAX(dst) = n + 1; }
    if (AS_MAX(dst) < n + 1) { c->r[0] = STATUS_BUFFER_OVERFLOW; X_RET(3); }
    for (unsigned i = 0; i < n; ++i) { uint16_t w = X_M16(AS_BUF(src) + i * 2); X_W8(AS_BUF(dst) + i) = w < 256 ? (uint8_t)w : '?'; }
    X_W8(AS_BUF(dst) + n) = 0; AS_LEN(dst) = n;
    c->r[0] = STATUS_SUCCESS; X_RET(3);
}
void xk_RtlAnsiStringToUnicodeString(xctx *c)
{
    uint32_t dst = X_ARG(0), src = X_ARG(1); int alloc = X_ARG(2) & 0xFF;
    unsigned n = AS_LEN(src);
    if (alloc) { AS_BUF(dst) = xk_kalloc(n * 2 + 2); AS_MAX(dst) = n * 2 + 2; }
    if (AS_MAX(dst) < n * 2 + 2) { c->r[0] = STATUS_BUFFER_OVERFLOW; X_RET(3); }
    for (unsigned i = 0; i < n; ++i) X_W16(AS_BUF(dst) + i * 2) = X_M8(AS_BUF(src) + i);
    X_W16(AS_BUF(dst) + n * 2) = 0; AS_LEN(dst) = n * 2;
    c->r[0] = STATUS_SUCCESS; X_RET(3);
}
void xk_RtlFreeAnsiString(xctx *c) { AS_BUF(X_ARG(0)) = 0; AS_LEN(X_ARG(0)) = 0; X_RET(1); }
void xk_RtlFreeUnicodeString(xctx *c) { AS_BUF(X_ARG(0)) = 0; AS_LEN(X_ARG(0)) = 0; X_RET(1); }
void xk_RtlUnicodeToMultiByteN(xctx *c)
{
    uint32_t dst = X_ARG(0), max = X_ARG(1), pres = X_ARG(2), src = X_ARG(3), n = X_ARG(4) / 2;
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; ++i) { uint16_t w = X_M16(src + i * 2); X_W8(dst + i) = w < 256 ? (uint8_t)w : '?'; }
    if (pres) X_W32(pres) = n;
    c->r[0] = STATUS_SUCCESS; X_RET(5);
}
void xk_RtlMultiByteToUnicodeN(xctx *c)
{
    uint32_t dst = X_ARG(0), max = X_ARG(1) / 2, pres = X_ARG(2), src = X_ARG(3), n = X_ARG(4);
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; ++i) X_W16(dst + i * 2) = X_M8(src + i);
    if (pres) X_W32(pres) = n * 2;
    c->r[0] = STATUS_SUCCESS; X_RET(5);
}

/* ---- critical sections: RTL_CRITICAL_SECTION { Event/Synchronization[16]; LockCount; RecursionCount; OwningThread } (Xbox layout: 28 bytes) */
#define CS_LOCKCOUNT(cs)  X_M32((cs) + 16)
#define CS_RECURSION(cs)  X_M32((cs) + 20)
#define CS_OWNER(cs)      X_M32((cs) + 24)
/* True when a critical section's owner (a guest KTHREAD address) belongs to a thread that has exited -
 * such a lock would otherwise deadlock every waiter (mainCRTStartup takes locks then exits). */
static int cs_owner_dead(uint32_t owner_kthread)
{
    xk_obj *o = xk_obj_from_guest(owner_kthread);
    return o && o->type == XO_THREAD && o->u.thread->state == 3;
}
void xk_RtlInitializeCriticalSection(xctx *c) { uint32_t cs = X_ARG(0); CS_LOCKCOUNT(cs) = 0xFFFFFFFFu; CS_RECURSION(cs) = 0; CS_OWNER(cs) = 0; X_RET(1); }
void xk_RtlEnterCriticalSection(xctx *c)
{
    uint32_t cs = X_ARG(0), me = xk_cur ? xk_cur->kthread : 1;
    if (cs) {
        uint64_t spin_t0 = 0;
        while (CS_OWNER(cs) && CS_OWNER(cs) != me && (int32_t)CS_LOCKCOUNT(cs) >= 0) {
            if (cs_owner_dead(CS_OWNER(cs))) break;      /* owner thread exited holding it: steal */
            {   /* 3 s of spinning is never legitimate: log the cycle (Vita perf112/116 froze here: the owner acquiring on the
                 * scene helper's behalf while the holder waits for the owner's tick) and steal the section rather than freeze */
                extern uint64_t xk_os_monotonic_us(void); uint64_t now = xk_os_monotonic_us(); if (!spin_t0) spin_t0 = now;
                if (now - spin_t0 > 3000000u) {
                    xk_obj *ho = xk_obj_from_guest(CS_OWNER(cs)); xk_thread *ht = ho && ho->type == XO_THREAD ? ho->u.thread : NULL;
                    XK_LOG("[cs] STUCK 3 s: t%d wants critical section %08X held by kthread %08X (t%d state %d wait_n %d obj %p) lock %d rec %d - stealing\n",
                           xk_cur ? xk_cur->id : -1, cs, CS_OWNER(cs), ht ? ht->id : -1, ht ? ht->state : -1, ht ? ht->wait_n : -1, ht && ht->wait_n ? (void *)ht->wait_objs[0] : NULL, (int)CS_LOCKCOUNT(cs), (int)CS_RECURSION(cs));
                    CS_LOCKCOUNT(cs) = 0xFFFFFFFFu; break;
                }
            }
            xk_yield();
        }
        if (CS_OWNER(cs) == me) CS_RECURSION(cs)++; else { CS_OWNER(cs) = me; CS_RECURSION(cs) = 1; CS_LOCKCOUNT(cs) = 0xFFFFFFFFu; }
        CS_LOCKCOUNT(cs)++;
    }
    X_RET(1);
}
void xk_RtlTryEnterCriticalSection(xctx *c)
{
    uint32_t cs = X_ARG(0), me = xk_cur ? xk_cur->kthread : 1;
    if (CS_OWNER(cs) && CS_OWNER(cs) != me && (int32_t)CS_LOCKCOUNT(cs) >= 0 && !cs_owner_dead(CS_OWNER(cs))) { c->r[0] = 0; X_RET(1); }
    if (cs_owner_dead(CS_OWNER(cs))) CS_LOCKCOUNT(cs) = 0xFFFFFFFFu;
    if (CS_OWNER(cs) == me) CS_RECURSION(cs)++; else { CS_OWNER(cs) = me; CS_RECURSION(cs) = 1; }
    CS_LOCKCOUNT(cs)++; c->r[0] = 1; X_RET(1);
}
void xk_RtlLeaveCriticalSection(xctx *c)
{
    uint32_t cs = X_ARG(0);
    if (cs && CS_OWNER(cs)) { CS_LOCKCOUNT(cs)--; if (--CS_RECURSION(cs) == 0) CS_OWNER(cs) = 0; }
    X_RET(1);
}
void xk_RtlEnterCriticalSectionAndRegion(xctx *c) { xk_RtlEnterCriticalSection(c); }
void xk_RtlLeaveCriticalSectionAndRegion(xctx *c) { xk_RtlLeaveCriticalSection(c); }

/* ---- time --------------------------------------------------------------------------------- */
/* TIME_FIELDS { SHORT Year, Month, Day, Hour, Minute, Second, Millisecond, Weekday } */
static int days_from_civil(int y, int m, int d) { y -= m <= 2; int era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = y - era * 400; unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; return era * 146097 + (int)doe - 719468; }
void xk_RtlTimeFieldsToTime(xctx *c)
{
    uint32_t tf = X_ARG(0), out = X_ARG(1);
    int y = (int16_t)X_M16(tf), mo = (int16_t)X_M16(tf + 2), d = (int16_t)X_M16(tf + 4), h = (int16_t)X_M16(tf + 6), mi = (int16_t)X_M16(tf + 8), s = (int16_t)X_M16(tf + 10), ms = (int16_t)X_M16(tf + 12);
    int64_t days = days_from_civil(y, mo, d) + 134774;   /* 1601 -> 1970 offset in days */
    LI64(out) = (uint64_t)((((days * 24 + h) * 60 + mi) * 60 + s) * 1000 + ms) * 10000ull;
    c->r[0] = 1; X_RET(2);
}
void xk_RtlTimeToTimeFields(xctx *c)
{
    uint64_t t = LI64(X_ARG(0)); uint32_t tf = X_ARG(1);
    uint64_t ms = t / 10000; int64_t days = (int64_t)(ms / 86400000ull) - 134774; unsigned rem = (unsigned)(ms % 86400000ull);
    int64_t z = days + 719468; int64_t era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; int y = (int)yoe + (int)era * 400; unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153; unsigned d = doy - (153 * mp + 2) / 5 + 1; unsigned m = mp + (mp < 10 ? 3 : -9); y += m <= 2;
    X_W16(tf) = (uint16_t)y; X_W16(tf + 2) = (uint16_t)m; X_W16(tf + 4) = (uint16_t)d;
    X_W16(tf + 6) = rem / 3600000; X_W16(tf + 8) = rem / 60000 % 60; X_W16(tf + 10) = rem / 1000 % 60; X_W16(tf + 12) = rem % 1000;
    X_W16(tf + 14) = (uint16_t)((days + 134774 + 1) % 7);
    X_RET(2);
}

/* ---- misc ----------------------------------------------------------------------------------- */
void xk_RtlNtStatusToDosError(xctx *c)
{
    uint32_t s = X_ARG(0), e;
    switch (s) {
    case STATUS_SUCCESS: e = 0; break;
    case STATUS_NO_SUCH_FILE: case STATUS_OBJECT_NAME_NOT_FOUND: e = 2; break;      /* ERROR_FILE_NOT_FOUND */
    case STATUS_OBJECT_PATH_NOT_FOUND: e = 3; break;                                 /* ERROR_PATH_NOT_FOUND */
    case STATUS_ACCESS_DENIED: case STATUS_CANNOT_DELETE: e = 5; break;
    case STATUS_INVALID_HANDLE: e = 6; break;
    case STATUS_NO_MEMORY: case STATUS_INSUFFICIENT_RESOURCES: e = 8; break;
    case STATUS_INVALID_PARAMETER: e = 87; break;
    case STATUS_END_OF_FILE: e = 38; break;
    case STATUS_NO_MORE_FILES: e = 18; break;
    case STATUS_OBJECT_NAME_COLLISION: e = 183; break;                               /* ERROR_ALREADY_EXISTS */
    case STATUS_DIRECTORY_NOT_EMPTY: e = 145; break;
    case STATUS_BUFFER_TOO_SMALL: case STATUS_BUFFER_OVERFLOW: e = 122; break;
    case STATUS_NOT_IMPLEMENTED: e = 120; break;
    case STATUS_TIMEOUT: e = 1460; break;
    default: e = (s & 0xC0000000u) == 0xC0000000u ? 317 : 0; break;                  /* ERROR_MR_MID_NOT_FOUND */
    }
    c->r[0] = e; X_RET(1);
}
void xk_RtlRaiseException(xctx *c) { XK_LOG("RtlRaiseException(record %08X code %08X)\n", X_ARG(0), X_M32(X_ARG(0))); xv_trap(c, X_M32(X_ARG(0) + 12)); }
void xk_RtlRaiseStatus(xctx *c) { XK_LOG("RtlRaiseStatus(%08X)\n", X_ARG(0)); xv_trap(c, 0); }
void xk_RtlAssert(xctx *c) { XK_LOG("RtlAssert: %s (%s:%u)\n", xk_gstr(X_ARG(0)), xk_gstr(X_ARG(1)), X_ARG(2)); X_RET(4); }
void xk_RtlCaptureContext(xctx *c) { X_RET(1); }
void xk_RtlUnwind(xctx *c) { XK_LOG("RtlUnwind - SEH not supported\n"); xv_trap(c, 0); }
/* RtlSnprintf/RtlSprintf family: format with guest varargs (ints/pointers/doubles) */
static int gfmt(char *out, unsigned cap, const char *fmt, uint32_t args)
{
    char *p = out, *end = out + cap - 1;
    while (*fmt && p < end) {
        if (*fmt != '%') { *p++ = *fmt++; continue; }
        char spec[32]; unsigned si = 0; spec[si++] = *fmt++;
        while (*fmt && strchr("-+ #0123456789.lhI", *fmt) && si < 28) { if (*fmt != 'l' && *fmt != 'h' && *fmt != 'I') spec[si++] = *fmt; fmt++; }
        char cv = *fmt ? *fmt++ : 0; if (!cv) break;
        char buf[512];
        if (cv == 's') { spec[si++] = 's'; spec[si] = 0; snprintf(buf, sizeof buf, spec, xk_gstr(X_M32(args))); args += 4; }
        else if (cv == 'c') { spec[si++] = 'c'; spec[si] = 0; snprintf(buf, sizeof buf, spec, (int)X_M32(args)); args += 4; }
        else if (cv == 'f' || cv == 'g' || cv == 'e') { double d; memcpy(&d, X_G(args), 8); spec[si++] = cv; spec[si] = 0; snprintf(buf, sizeof buf, spec, d); args += 8; }
        else if (cv == '%') { buf[0] = '%'; buf[1] = 0; }
        else { spec[si++] = cv; spec[si] = 0; snprintf(buf, sizeof buf, spec, X_M32(args)); args += 4; }
        for (char *q = buf; *q && p < end; ++q) *p++ = *q;
    }
    *p = 0; return (int)(p - out);
}
void xk_RtlSprintf(xctx *c) { char buf[2048]; int n = gfmt(buf, sizeof buf, xk_gstr(X_ARG(1)), c->r[4] + 12); strcpy(X_G(X_ARG(0)), buf); c->r[0] = n; c->r[4] += 4; return; }   /* cdecl */
void xk_RtlSnprintf(xctx *c) { char buf[2048]; int n = gfmt(buf, sizeof buf, xk_gstr(X_ARG(2)), c->r[4] + 16); uint32_t cap = X_ARG(1); if ((unsigned)n >= cap) n = cap ? (int)cap - 1 : 0; memcpy(X_GWN(X_ARG(0), n), buf, n); if (cap) X_W8(X_ARG(0) + n) = 0; c->r[0] = n; c->r[4] += 4; return; }
void xk_RtlVsprintf(xctx *c) { char buf[2048]; int n = gfmt(buf, sizeof buf, xk_gstr(X_ARG(1)), X_ARG(2)); strcpy(X_G(X_ARG(0)), buf); c->r[0] = n; c->r[4] += 4; return; }
void xk_RtlVsnprintf(xctx *c) { char buf[2048]; int n = gfmt(buf, sizeof buf, xk_gstr(X_ARG(2)), X_ARG(3)); uint32_t cap = X_ARG(1); if ((unsigned)n >= cap) n = cap ? (int)cap - 1 : 0; memcpy(X_GWN(X_ARG(0), n), buf, n); if (cap) X_W8(X_ARG(0) + n) = 0; c->r[0] = n; c->r[4] += 4; return; }
void xk_DbgPrint(xctx *c) { char buf[2048]; gfmt(buf, sizeof buf, xk_gstr(X_ARG(0)), c->r[4] + 8); XK_LOG("DbgPrint: %s", buf); if (!strchr(buf, '\n')) fputc('\n', stderr); c->r[0] = 0; c->r[4] += 4; return; }
