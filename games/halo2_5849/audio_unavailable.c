/* Explicit failure-path diagnostic. This is not a DirectSound backend. */
#include "gpu_bus.h"
#include "nv2a_regs.h"
extern uint32_t xk_mem_arena_size(void);
extern void xv_logf(const char *, ...);
static int mapped(uint32_t address, uint32_t bytes)
{
    uint32_t arena = xk_mem_arena_size();
    if (!address || !bytes || arena < 4096 || (uint64_t)address + bytes > UINT32_MAX + 1ull) return 0;
    uint32_t last = (address + bytes - 1) >> 12;
    for (uint32_t page = address >> 12; page <= last; ++page)
        if ((g_xpt[page] & 4095) || (uint64_t)g_xpt[page] + 4096 > arena - 4096) return 0;
    return 1;
}
void h2_audio_unavailable(xctx *c)
{
    if ((c->r[4] & 3) || !mapped(c->r[4], 16))
        h2_graphics_stop(c, 0x37D797, c->r[4], 16, 0, H2_NV2A_UNSUPPORTED_OPERATION);
    uint32_t output = X_ARG(1);
    if (X_ARG(0) || X_ARG(2) || !mapped(output, 4))
        h2_graphics_stop(c, 0x37D797, output, X_ARG(0) | X_ARG(2), 0, H2_NV2A_UNSUPPORTED_OPERATION);
    xv_logf("[h2/diagnostic] DirectSoundCreate caller=%08X args=0,%08X,0 returns DSERR_NODRIVER=88780078; no audio device; output untouched\n",
            X_M32(c->r[4]), output);
    /* The original 5849 wrapper writes the output only after a successful
     * HRESULT. Preserve every guest byte and all CPU state except EAX/ESP. */
    c->r[0] = 0x88780078; X_RET(3);
}
