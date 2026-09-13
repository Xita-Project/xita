#pragma once
#include <stdint.h>
#include "kernel/xk.h"

/* Diagnostic virtual volumes are fixed at 750 MiB, with 512-byte sectors.
 * These are configured capacities, not geometry inferred from the host disk. */
#define H2_CACHE_CAPACITY (750u * 1024u * 1024u)
#define H2_SAVE_ROOT "ux0:data/xita-halo2/save"
int h2_cache_mounts(void);
int h2_cache_validate_empty(xk_file *file);
