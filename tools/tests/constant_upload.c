#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static struct { float vsc[192][4]; uint32_t vsc_dirty_lo, vsc_dirty_hi; } xd3d_state;
typedef struct { uint32_t args[3], r[8]; unsigned popped; } xctx;
#define X_ARG(n) (c->args[n])
#define X_RET(n) do { c->popped=(n); return; } while (0)
#define X_M32(a) ((uint32_t)(a))
#define XD3D_COUNT(name) ((void)(name))
static int hist_mode;
static int xd3d_hist_active(void) { return hist_mode; }
static void test_log(const char *format, ...) { (void)format; }
#define D3DLOG(...) test_log(__VA_ARGS__)

/* Deterministic readable guest mappings, including unsigned address wrap.
 * Record every requested address; the reference independently enumerates
 * valid destination rows instead of reproducing the producer's loop. */
static uint32_t addresses[512], address_count;
static int page_mode;
static unsigned char page_store[4][8192];
static unsigned page_slot(uint32_t address) { return (address >> 12) == 0xfffffu ? 3u : ((address >> 12) & 1u) * 2u; }
static void sample(uint32_t address, uint32_t bits[4])
{
    bits[0] = address * 2654435761u;
    bits[1] = address ^ 0x80000000u;
    bits[2] = address & 16 ? 0x7fc12345u : 0x7f800000u;
    bits[3] = address & 32 ? 0xff800000u : 0x80000000u;
}
static const void *guest(uint32_t address)
{
    static uint32_t words[4];
    assert(address_count < 512); addresses[address_count++] = address;
    if (page_mode) return page_store[page_slot(address)] + (address & 4095u);
    sample(address, words); return words;
}
#define X_G(a) guest((a))
#include "constant_read.inc"
#include "constant_upload.inc"

int main(void)
{
    const int32_t starts[] = {INT32_MIN, -1000, -289, -193, -192, -97, -96, -95,
                             -1, 0, 94, 95, 96, 191, 1000, INT32_MAX};
    const uint32_t counts[] = {0, 1, 2, 4, 16, 96, 191, 192, 193, 400, UINT32_MAX};
    unsigned cases = 0;
    for (unsigned dirty = 0; dirty < 3; dirty++)
        for (unsigned a = 0; a < sizeof starts / sizeof *starts; a++)
            for (unsigned n = 0; n < sizeof counts / sizeof *counts; n++) {
                memset(xd3d_state.vsc, 0xa5, sizeof xd3d_state.vsc);
                xd3d_state.vsc_dirty_lo = dirty == 1 ? 40 : dirty == 2 ? 0 : 192;
                xd3d_state.vsc_dirty_hi = dirty == 1 ? 80 : 0;
                float expected[192][4]; memcpy(expected, xd3d_state.vsc, sizeof expected);
                uint32_t lo = xd3d_state.vsc_dirty_lo, hi = xd3d_state.vsc_dirty_hi;
                uint32_t expected_addresses[192], expected_count = 0;
                uint32_t source = cases & 1 ? 0xfffffff0u : 0x2000;
                for (unsigned row = 0; row < 192; row++) {
                    int64_t index = (int64_t)row - ((int64_t)starts[a] + 96);
                    if (index < 0 || (uint64_t)index >= counts[n]) continue;
                    uint32_t address = source + (uint32_t)index * 16u, bits[4];
                    expected_addresses[expected_count++] = address;
                    sample(address, bits);
                    for (unsigned k = 0; k < 4; k++)
                        if ((bits[k] & 0x7f800000u) == 0x7f800000u) bits[k] = 0;
                    memcpy(expected[row], bits, sizeof bits);
                    if (row < lo) lo = row;
                    if (row + 1 > hi) hi = row + 1;
                }
                xctx c = {{(uint32_t)starts[a], source, counts[n]}, {0}, 0};
                address_count = 0; c.r[0] = 123;
                xv_hle_D3DDevice_SetVertexShaderConstant(&c);
                assert(c.popped == 3 && c.r[0] == 0);
                assert(address_count == expected_count);
                assert(!memcmp(addresses, expected_addresses, address_count * sizeof *addresses));
                assert(!memcmp(xd3d_state.vsc, expected, sizeof expected));
                assert(xd3d_state.vsc_dirty_lo == lo && xd3d_state.vsc_dirty_hi == hi);
                cases++;
            }
    /* Physical pages include poison gaps. A direct 16-byte host copy crossing
     * a guest boundary must fail this test, including guest address wrap. */
    page_mode = 1;
    for (hist_mode = 0; hist_mode <= 1; hist_mode++)
    for (unsigned offset = 4081; offset <= 4097; offset++) {
        for (unsigned wrap = 0; wrap < 2; wrap++) {
            uint32_t source = (wrap ? 0xffffe000u : 0u) + offset;
            if (wrap) source += 4096u;
            memset(page_store, 0xa5, sizeof page_store);
            uint32_t expected[8][4];
            for (unsigned row = 0; row < 8; row++) {
                uint32_t words[4] = {0x3f800000u + row, 0x80000000u, 0x7fc12345u, 0xff800000u};
                for (unsigned b = 0; b < 16; b++) {
                    uint32_t a = source + row * 16 + b;
                    page_store[page_slot(a)][a & 4095u] = ((unsigned char *)words)[b];
                }
                words[2] = words[3] = 0;
                memcpy(expected[row], words, sizeof words);
            }
            memset(&xd3d_state, 0, sizeof xd3d_state);
            xd3d_state.vsc_dirty_lo = 192;
            xctx c = {{(uint32_t)-96, source, 8}, {0}, 0};
            address_count = 0;
            xv_hle_D3DDevice_SetVertexShaderConstant(&c);
            assert(!memcmp(xd3d_state.vsc, expected, sizeof expected));
            assert(c.popped == 3 && c.r[0] == 0);
            assert(xd3d_state.vsc_dirty_lo == 0 && xd3d_state.vsc_dirty_hi == 8);
            cases++;
        }
    }
    printf("PASS: %u production uploads, clipping, dirty unions, non-finite values, split pages and address wrap\n", cases);
}
