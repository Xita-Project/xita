#pragma once
#include <stdint.h>
/* Budget for per-frame / per-call diagnostics: the first `first` events log in
 * full, then one in every `every` (0 = none). The strict-stop context stays in
 * the log without paying a sceClibPrintf per instruction or per DPC. */
static inline int h2_log_budget(uint32_t *counter, uint32_t first, uint32_t every)
{ uint32_t n = (*counter)++; return n < first || (every && n % every == 0); }
