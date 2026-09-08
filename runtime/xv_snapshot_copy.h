#ifndef XV_SNAPSHOT_COPY_H
#define XV_SNAPSHOT_COPY_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

/* Publish identical bytes to the cached comparison mirror and uncached GPU
 * snapshot from the same loads. The three ranges must not overlap. This does
 * not acquire a slot, flush caches or change the caller's completion barrier. */
static inline void xv_snapshot_copy(void *mirror, void *gpu, const void *source, size_t bytes)
{
#if defined(__ARM_NEON)
    uint8_t *cpu = mirror, *out = gpu;
    const uint8_t *in = source;
    while (bytes >= 64) {
        uint8x16_t a = vld1q_u8(in), b = vld1q_u8(in + 16);
        uint8x16_t c = vld1q_u8(in + 32), d = vld1q_u8(in + 48);
        vst1q_u8(cpu, a); vst1q_u8(cpu + 16, b);
        vst1q_u8(cpu + 32, c); vst1q_u8(cpu + 48, d);
        vst1q_u8(out, a); vst1q_u8(out + 16, b);
        vst1q_u8(out + 32, c); vst1q_u8(out + 48, d);
        in += 64; cpu += 64; out += 64; bytes -= 64;
    }
    while (bytes >= 16) {
        uint8x16_t a = vld1q_u8(in);
        vst1q_u8(cpu, a); vst1q_u8(out, a);
        in += 16; cpu += 16; out += 16; bytes -= 16;
    }
    while (bytes--) {
        uint8_t value = *in++;
        *cpu++ = value; *out++ = value;
    }
#else
    if (bytes) { memcpy(mirror, source, bytes); memcpy(gpu, mirror, bytes); }
#endif
}
#endif
