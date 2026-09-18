#include "fp_environment.h"
#include "log_budget.h"
#include <stdint.h>
static uint32_t stmxcsr_logs, ldmxcsr_logs;
extern uint32_t xk_mem_arena_size(void);
extern void xv_logf(const char *, ...);

static int mapped(uint32_t address)
{
    uint32_t arena = xk_mem_arena_size();
    if (!address || address > UINT32_MAX - 3 || arena < 4096) return 0;
    for (uint32_t page = address >> 12; page <= (address + 3) >> 12; ++page)
        if ((g_xpt[page] & 4095) || (uint64_t)g_xpt[page] + 4096 > arena - 4096) return 0;
    return 1;
}
static int supported_native(uint32_t value)
{
    /* FPSCR DN/FZ/RMode/Stride/Len and exception-enable controls must be zero.
     * Preserve unrelated NZCV/QC/AHP and reserved bits; no arithmetic occurs here. */
    return !(value & 0x03F79F00u);
}
void h2_stmxcsr(xctx *c, uint32_t ip, uint32_t address)
{
    if (!mapped(address)) { h2_fp_environment_fault(c, ip, address, 0); return; }
    uint32_t native = h2_platform_fpscr_read();
    if (!supported_native(native)) { h2_fp_environment_fault(c, ip, address, native); return; }
    uint32_t value = 0x1F80u | (native & 1) | ((native >> 6) & 2) | ((native & 0x1E) << 1);
    x_guest_write(address, &value, 4);
    if (h2_log_budget(&stmxcsr_logs, 64, 100000)) xv_logf("[h2/fp] STMXCSR ip=%08X native=%08X value=%08X\n", ip, native, value);
    h2_platform_fpscr_write(native); /* diagnostics must not alter exception history */
}
void h2_ldmxcsr(xctx *c, uint32_t ip, uint32_t address)
{
    if (!mapped(address)) { h2_fp_environment_fault(c, ip, address, 0); return; }
    uint32_t value; x_guest_read(&value, address, 4);
    uint32_t native = h2_platform_fpscr_read();
    /* All exception masks set; RC=nearest, FTZ=0, DAZ=0, reserved upper bits=0.
     * Accept/retain status bits, never silently accept unsupported controls. */
    if ((value & ~0x3Fu) != 0x1F80 || !supported_native(native)) {
        h2_fp_environment_fault(c, ip, address, value); return;
    }
    uint32_t flags = (value & 1) | ((value & 2) << 6) | ((value >> 1) & 0x1E);
    uint32_t result = (native & ~0x9Fu) | flags;
    if (h2_log_budget(&ldmxcsr_logs, 64, 100000)) xv_logf("[h2/fp] LDMXCSR ip=%08X value=%08X native=%08X\n", ip, value, result);
    h2_platform_fpscr_write(result);
}
