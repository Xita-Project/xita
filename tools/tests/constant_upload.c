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
static int xd3d_hist_active(void) { return 0; }
static void test_log(const char *format, ...) { (void)format; }
#define D3DLOG(...) test_log(__VA_ARGS__)

/* Deterministic readable guest mappings, including unsigned address wrap.
 * Record every requested address; the reference independently enumerates
 * valid destination rows instead of reproducing the producer's loop. */
static uint32_t addresses[192], address_count;
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
    assert(address_count < 192); addresses[address_count++] = address;
    sample(address, words); return words;
}
#define X_G(a) guest((a))
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
    printf("PASS: %u production uploads, clipping, dirty unions, non-finite values and address wrap\n", cases);
}
