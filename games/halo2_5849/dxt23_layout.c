#include "dxt23_layout.h"
#include <string.h>

int h2_dxt23_gxm_8x8(const uint8_t *source, size_t source_bytes,
                       uint8_t *destination, size_t destination_bytes)
{
    if (!source || !destination || source_bytes != 64 || destination_bytes != 64 ||
        (uintptr_t)source > UINTPTR_MAX - 63 ||
        (uintptr_t)destination > UINTPTR_MAX - 63) return 0;
    /* NV2A row order: TL,TR,BL,BR. SceGxmTextureInitSwizzled BC2 8x8:
     * TL,BL,TR,BR. The 16 bytes inside each block remain in BC2 encoding. */
    uint8_t copied[64];
    memcpy(copied, source, 64);
    memcpy(destination, copied, 16);
    memcpy(destination + 16, copied + 32, 16);
    memcpy(destination + 32, copied + 16, 16);
    memcpy(destination + 48, copied + 48, 16);
    return 1;
}
