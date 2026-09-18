#pragma once
#include "dsp_engine.h"
/* Reads only the versioned private asset produced by prepare_dsp.py. The
 * preparation step validates SHA-256 of the owned XBE/monitor; runtime FNV
 * fingerprints detect accidental mismatched/corrupt payloads. */
h2_dsp_engine *h2_dsp_asset_open(const char *path, h2_dsp_status *status);
