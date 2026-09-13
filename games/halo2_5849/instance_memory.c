/* Retail instance allocation contract, independently implemented from the
 * pinned Cxbx ClaimGpuMemory behavior. Guest address at the top of the active
 * allocation is stable; the allocation shrinks from its lower end. */
#include "instance_memory.h"
#include "kernel/xk.h"
#include "gpu_bus.h"
#include "nv2a_regs.h"

#define INSTANCE_END 0x03FF0000u
#define INSTANCE_INITIAL 0x10000u
static uint32_t reserved_bytes;
static int initialized;
extern void xv_logf(const char *format, ...);

int h2_instance_memory_init(void)
{
    if (initialized || xk_kreserve_fixed(INSTANCE_END - INSTANCE_INITIAL, INSTANCE_INITIAL)) return -1;
    reserved_bytes = INSTANCE_INITIAL;
    initialized = 1;
    return 0;
}
uint32_t h2_instance_bytes(void) { return reserved_bytes; }
uint32_t h2_instance_claim(uint32_t bytes, uint32_t *padding)
{
    if (!initialized || !padding) return 0;
    if (bytes != UINT32_MAX) {
        if (bytes > reserved_bytes) return 0; /* regrowth is not the shrink contract */
        uint32_t rounded = (bytes + 4095u) & ~4095u;
        if (rounded < reserved_bytes &&
            xk_krelease_fixed(INSTANCE_END - reserved_bytes, reserved_bytes - rounded)) return 0;
        reserved_bytes = rounded;
    }
    *padding = 0x10000;
    return 0x80000000u | INSTANCE_END;
}
void __wrap_xk_MmClaimGpuInstanceMemory(xctx *context)
{
    xctx *c = context;
    uint32_t requested = X_ARG(0), output = X_ARG(1), padding;
    uint32_t end = output ? h2_instance_claim(requested, &padding) : 0;
    if (!end) h2_graphics_stop(c, X_M32(c->r[4]), output, requested, 1, H2_NV2A_UNSUPPORTED_OPERATION);
    x_guest_write(output, &padding, sizeof padding);
    xv_logf("[h2/instance] claim=%08X output=%08X padding=%08X end=%08X reserved=%08X\n",
            requested, output, padding, end, reserved_bytes);
    c->r[0] = end;
    X_RET(2);
}
