/* Optional Halo 3925 object-reference walk, 0x172034..0x172163 in 0x171F10.
 *
 * The original visits each collected BSP leaf, stamps its cluster, walks that
 * cluster's object-reference chain, stamps each unvisited object and calls
 * 0x1716F0 through a six-argument guest frame. This walk performs the same
 * guest reads and writes in the same order and calls the translated 0x1716F0
 * for every object whose outcome it cannot establish. It replaces only that
 * callee's early exits which make no call and pass no back edge.
 *
 * A replaced exit still performs every guest store the original makes: the
 * seven pushed words and the callee's four saved registers and flag local,
 * all before any callee read, so inputs aliasing that frame read the same
 * bytes. Registers, lazy flags, x87 slots and status word are reproduced at
 * every observation point. The game profile validates both complete original
 * functions before hooking.
 */
#ifdef XV_NATIVE_OBJECT_COLLECT
#include "xk.h"
#include <stdlib.h>

void f_001716F0(xctx *);

enum { NONE = 0xFFFFFFFFu };

/* Callers of 0x171F10 are not all proven to share one guard, so configuration
 * and diagnostic counters use atomic storage. Each walk tallies privately and
 * publishes once; a report is not a consistent snapshot of all four counts. */
static int collect_active = -1;
static unsigned collect_walks, collect_leaves, collect_objects, collect_skipped;

static int collect_enabled(void)
{
    int value = __atomic_load_n(&collect_active, __ATOMIC_RELAXED);
    if (value >= 0) return value;
    const char *setting = getenv("XV_NATIVE_OBJECT_COLLECT");
    int expected = -1;
    value = setting && atoi(setting) != 0;
    __atomic_compare_exchange_n(&collect_active, &expected, value, 0,
                                __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
    return __atomic_load_n(&collect_active, __ATOMIC_RELAXED) > 0;
}

/* A walk already admitted finishes natively; either route is exact. */
void xv_object_collect_override(int value)
{
    __atomic_store_n(&collect_active, value < 0 ? -1 : !!value, __ATOMIC_RELEASE);
}

static int native_math_allowed(void)
{
#if defined(__arm__)
    /* Unmasked native traps retain the translated callee arithmetic. */
    uint32_t fpscr;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr) :: "memory");
    if (fpscr & 0x00009f00u) return 0;
#endif
    return 1;
}

/* Entered in the state of the call at 0x172111, after its seven pushes:
 * ESP is the return word, ECX the flags and EDX the object datum. Returns 1
 * in the state after 0x1716F0's `ret 18h` for exits without a call or back
 * edge. A decline leaves c unchanged; the only guest writes it has made are
 * the callee prologue's, which the translated callee repeats with the same
 * values before any read. */
static int reject_object(xctx *c)
{
    uint32_t sp = c->r[4], flags = c->r[1], datum = c->r[2];
    uint32_t frame = sp - 0x74u;
    /* 0x1716F0..0x1716FB, in order. Exits restore registers by reading these
     * words back, as the original pops do, so aliases need no separate proof. */
    X_M32(frame + 0x0Cu) = c->r[3];
    X_M32(frame + 0x08u) = c->r[5];
    X_M32(frame + 0x04u) = c->r[6];
    X_M32(frame) = c->r[7];
    X_M32(frame + 0x10u) = flags;
    /* Only reads follow, so their order no longer matters. */
    uint32_t table = X_IMG32(0x2FC6ACu);
    uint32_t base = X_M32(table + 0x34u);
    uint32_t scaled = (datum & 0xFFFFu) * 3u;
    uint32_t object = X_M32(base + scaled * 4u + 8u);
    /* 0x17191D: the sibling loop holds the callee's only back edge. */
    if (X_M32(object + 0xC4u) != NONE) return 0;
    uint32_t eax, ecx = table, edx = base;
    if (datum == X_M32(frame + 0x88u)) {
        eax = scaled;
        goto finish;
    }
    uint32_t word = X_M32(object + 4u);
    eax = word;
    if ((word & 1u) || (word & 0x1000000u)) goto finish;
    if ((X_M8(object + 0xB6u) & 4u) && X_M16(object + 0x64u) == 0) goto finish;
    if (!native_math_allowed()) return 0;

    /* 0x17174E: bounds 50/54/58, radius 5C, argument radius and center. */
    uint32_t words[8];
    x_guest_read(words, object + 0x50u, 16);
    uint32_t center = X_M32(frame + 0x78u);
    words[4] = X_M32(frame + 0x7Cu);
    x_guest_read(words + 5, center, 12);
    /* Finite inputs cannot produce NaN, so the original quiet comparison and
     * this one raise the same native exceptions. Keep other inputs original. */
    for (unsigned i = 0; i < 8; i++)
        if ((words[i] & 0x7F800000u) == 0x7F800000u) return 0;
    float value[8];
    memcpy(value, words, sizeof value);
    double radius = (double)value[3] + (double)value[4];
    double dx = (double)value[0] - (double)value[5];
    double dy = (double)value[1] - (double)value[6];
    double dz = (double)value[2] - (double)value[7];
    double horizontal = dx * dx + dz * dz;
    double distance = horizontal + dy * dy;
    double limit = radius * radius;
    uint32_t top = c->fsp;
    uint16_t cc = limit < distance ? 0x0100u : limit == distance ? 0x4000u : 0u;
    uint16_t status = (uint16_t)((c->fsw & ~0x4700u) | cc | (((top - 6u) & 7u) << 11));
    uint32_t carry = c->f_cf, overflow = c->f_of;
    if (cc & 0x0100u) {
        /* FNSTSW AX replaces the low word of the loaded object flags. */
        eax = (word & 0xFFFF0000u) | status;
    } else {
        /* 0x171793: type mask, jump-table selector and child list. */
        eax = (uint32_t)(int32_t)(int16_t)X_M16(object + 0x64u);
        ecx = eax + 8u;
        unsigned shift = ecx & 31u;
        edx = 1u << shift;
        if (shift) {
            carry = 0;
            overflow = shift == 31u;
        }
        if ((flags & edx) && eax <= 8u) {
            ecx = X_M8(eax + 0x171944u);
            if (ecx != 2u) return 0;
        }
        if (X_M32(object + 0xC8u) != NONE) return 0;
        edx = NONE;
    }
    c->st[(top - 1u) & 7u] = radius;
    c->st[(top - 2u) & 7u] = dx;
    c->st[(top - 3u) & 7u] = dy;
    c->st[(top - 4u) & 7u] = dz;
    c->st[(top - 5u) & 7u] = distance;
    c->st[(top - 6u) & 7u] = limit;
    c->fsw = status;
    c->f_cf = carry;
    c->f_of = overflow;
finish:
    /* 0x17192C..0x171933: restore from the stored words, then return. */
    c->r[7] = X_M32(frame);
    c->r[6] = X_M32(frame + 0x04u);
    c->r[5] = X_M32(frame + 0x08u);
    c->r[3] = X_M32(frame + 0x0Cu);
    c->r[4] = sp + 0x1Cu;
    c->r[0] = eax;
    c->r[1] = ecx;
    c->r[2] = edx;
    /* 0x171923 compares the absent sibling with -1. */
    X_FLAGS(XK_SUB, NONE, NONE, 0u, 32);
    return 1;
}

int xv_object_collect_refs(xctx *c)
{
    if (!collect_enabled()) return 0;
    uint32_t frame = c->r[4];
    int32_t count = (int32_t)X_M32(frame + 0xC20u);
    /* The loop compares a sign-extended 16-bit index; keep counts where that
     * equals the 32-bit index. The original always enters with EAX zero. */
    if (count <= 0 || count > 0x7FFF || c->r[0] != 0) return 0;
    unsigned leaves = 0, objects = 0, skipped = 0;
    uint32_t eax = 0, ecx, edx, ebx, esi = c->r[6], ebp = c->r[5], edi = c->r[7];
    uint32_t compare_a, compare_b;

#define SYNC() do { c->r[0] = eax; c->r[1] = ecx; c->r[2] = edx; c->r[3] = ebx; \
        c->r[4] = frame; c->r[5] = ebp; c->r[6] = esi; c->r[7] = edi; } while (0)
#define RELOAD() do { eax = c->r[0]; ecx = c->r[1]; edx = c->r[2]; ebx = c->r[3]; \
        frame = c->r[4]; ebp = c->r[5]; esi = c->r[6]; edi = c->r[7]; } while (0)

    ebx = X_IMG32(0x2FC6A4u);
    for (;;) {
        /* 0x172040: leaf cluster and the shared cluster visitation stamp. */
        uint32_t leaf = X_M32(frame + eax * 4u + 0xC24u) & 0x7FFFFFFFu;
        ecx = X_IMG32(0x39BE58u);
        edx = X_M32(ecx + 0xE4u);
        eax = leaf << 4;
        c->f_cf = (leaf >> 28) & 1u;
        c->f_of = (eax >> 31) ^ c->f_cf;
        ecx = (uint32_t)(int32_t)(int16_t)X_M16(eax + edx + 8u);
        esi = X_M32(ecx * 4u + 0x2D2FB0u);
        eax += edx;
        edx = X_IMG32(0x2D2FACu);
        compare_a = esi;
        compare_b = edx;
        leaves++;
        if (esi != edx) {
            X_M32(ecx * 4u + 0x2D2FB0u) = edx;
            edx = (uint32_t)(int32_t)(int16_t)X_M16(eax + 8u);
            eax = X_IMG32(0x2FC6A0u);
            esi = X_M32(eax + edx * 4u);
            if (esi == NONE) {
                eax = NONE;
            } else {
                edx = X_M32(ebx + 0x34u);
                eax = esi & 0xFFFFu;
                ecx = eax * 3u;
                esi = X_M32(edx + ecx * 4u + 8u);
                eax = X_M32(edx + ecx * 4u + 4u);
            }
            compare_a = eax;
            compare_b = NONE;
            edx = eax;
            /* 0x1720B0 selects the chain once; the back edge at 0x172142
             * yields and then resumes 0x1720C0 without retesting. */
            if (eax != NONE) for (;;) {
                /* 0x1720C0: object stamp, then the shape callback. */
                ecx = X_IMG32(0x2FC6ACu);
                ecx = X_M32(ecx + 0x34u);
                eax = (edx & 0xFFFFu) * 3u;
                eax = X_M32(ecx + eax * 4u + 8u);
                uint32_t stamp = X_M32(eax + 8u);
                if (stamp != edi) {
                    objects++;
                    ecx = X_M32(frame + 0x103Cu);
                    X_M32(eax + 8u) = edi;
                    /* 0x1720E6..0x172111: argument order and reload points. */
                    uint32_t sp = frame;
                    eax = X_M32(sp + 0x1040u);
                    sp -= 4u; X_M32(sp) = eax;
                    eax = X_M32(sp + 0x103Cu);
                    sp -= 4u; X_M32(sp) = ecx;
                    ecx = X_M32(sp + 0x103Cu);
                    sp -= 4u; X_M32(sp) = eax;
                    eax = X_M32(sp + 0x103Cu);
                    sp -= 4u; X_M32(sp) = ecx;
                    ecx = X_M32(sp + 0x103Cu);
                    sp -= 4u; X_M32(sp) = eax;
                    sp -= 4u; X_M32(sp) = ecx;
                    sp -= 4u; X_M32(sp) = 0x172116u;
                    ecx = ebp;
                    SYNC();
                    c->r[4] = sp;
                    X_FLAGS(XK_SUB, stamp, edi, stamp - edi, 32);
                    if (reject_object(c)) skipped++;
                    else f_001716F0(c);
                    RELOAD();
                    ebx = X_IMG32(0x2FC6A4u);
                }
                if (esi == NONE) {
                    eax = NONE;
                } else {
                    eax = esi & 0xFFFFu;
                    edx = eax * 3u;
                    eax = X_M32(ebx + 0x34u);
                    esi = X_M32(eax + edx * 4u + 8u);
                    eax = X_M32(eax + edx * 4u + 4u);
                }
                compare_a = eax;
                compare_b = NONE;
                edx = eax;
                if (eax == NONE) break;
                SYNC();
                X_FLAGS(XK_SUB, eax, NONE, eax + 1u, 32);
                X_PREEMPT();
                RELOAD();
            }
        }
        /* 0x172148: INC keeps the carry of the preceding compare. */
        eax = X_M32(frame + 0x10u);
        ecx = X_M32(frame + 0xC20u);
        c->f_cf = compare_a < compare_b;
        eax++;
        X_M32(frame + 0x10u) = eax;
        eax = (uint32_t)(int32_t)(int16_t)eax;
        SYNC();
        X_FLAGS(XK_SUB, eax, ecx, eax - ecx, 32);
        if ((int32_t)eax >= (int32_t)ecx) break;
        /* 0x17215D resumes 0x172040 unconditionally after the yield. */
        X_PREEMPT();
        RELOAD();
    }
#undef SYNC
#undef RELOAD
    /* A job stopped inside a yield does not publish its partial tallies. */
    __atomic_add_fetch(&collect_walks, 1u, __ATOMIC_RELAXED);
    __atomic_add_fetch(&collect_leaves, leaves, __ATOMIC_RELAXED);
    __atomic_add_fetch(&collect_objects, objects, __ATOMIC_RELAXED);
    __atomic_add_fetch(&collect_skipped, skipped, __ATOMIC_RELAXED);
    return 1;
}

void xv_object_collect_report(unsigned frames)
{
    unsigned walks = __atomic_exchange_n(&collect_walks, 0u, __ATOMIC_RELAXED);
    unsigned leaves = __atomic_exchange_n(&collect_leaves, 0u, __ATOMIC_RELAXED);
    unsigned objects = __atomic_exchange_n(&collect_objects, 0u, __ATOMIC_RELAXED);
    unsigned skipped = __atomic_exchange_n(&collect_skipped, 0u, __ATOMIC_RELAXED);
    XK_LOG("[object-collect] %u frames walks %u leaves %u objects %u skipped %u\n",
           frames, walks, leaves, objects, skipped);
}
#endif
