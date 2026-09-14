#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct h2_dsp_engine h2_dsp_engine;
typedef struct {
    uint32_t code_offset, code_bytes, state_offset, state_bytes;
    uint32_t y_offset, y_bytes, scratch_offset, scratch_bytes;
} h2_dsp_effect;
typedef struct {
    uint64_t instructions, cycles, frames, transfers;
    uint64_t state_fingerprint; /* canonical DSP registers/stack/RAM/scratch, no host pointers */
    uint32_t pc, command, effect_count, scratch_bytes;
    uint32_t fault_address, fault_value;
    const char *fault;
} h2_dsp_status;

/* An isolated GP interpreter. Caller serializes all instances and calls.
 * Inputs are privately prepared owned DSP words, never embedded here.
 * Success requires the original monitor to acknowledge command 3 and reach
 * its frame halt after executing the uploaded program. No voice routing or
 * real-time scheduling is implied. Failure destroys the candidate instance. */
h2_dsp_engine *h2_dsp_create(const void *monitor, size_t monitor_bytes,
                            const void *image, size_t image_bytes,
                            h2_dsp_status *status);
void h2_dsp_destroy(h2_dsp_engine *engine);
void h2_dsp_snapshot(const h2_dsp_engine *engine, h2_dsp_status *status);
int h2_dsp_effect_map(const h2_dsp_engine *engine, uint32_t index, h2_dsp_effect *out);
int h2_dsp_read_effect(const h2_dsp_engine *engine, uint32_t index,
                       uint32_t offset, void *out, uint32_t bytes);
/* Tests/audit can execute another real frame with all input mix bins zero.
 * Any interpreter, DMA, memory or instruction-budget fault poisons the engine;
 * no API may report usable state afterward. */
int h2_dsp_zero_frame(h2_dsp_engine *engine);
