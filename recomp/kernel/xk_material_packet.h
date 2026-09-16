#pragma once
#include "../xv_x86rt.h"

enum { XV_MATERIAL_PACKET_ISLANDS = 5 };
typedef struct {
    uint32_t eligible[XV_MATERIAL_PACKET_ISLANDS];
    uint32_t accepted[XV_MATERIAL_PACKET_ISLANDS];
} xv_material_packet_counters;

/* Compile with XV_MATERIAL_PACKET=1 to opt in; ordinary builds are unavailable
 * and emit no guest hooks. Recording owner only, after the existing submission
 * drain and object join.
 * -1 restores XV_MATERIAL_PACKET (unset defaults OFF); 0/1 select an arm.
 * Neither interface waits, changes GPU ownership, nor mutates retained draws.
 * Counts saturate, and only the drained recording owner may read/reset them. */
void xv_material_packet_override(int enabled);
void xv_material_packet_read_counters(xv_material_packet_counters *out, int reset);
/* Drained owner query: runtime compatibility, not evidence of an executed
 * guarded island. A comparison must also observe nonzero acceptance counts. */
int xv_material_packet_available(void);

/* Only the exact-image/span-guarded generated islands may call this helper.
 * A decline preserves every context byte, guest byte and D3D state field.
 * Eligible owner calls count even while configured off; diagnostics/workers
 * and invalid arguments are rejected before touching owner counters. */
int xv_material_packet(xctx *c, unsigned island);
