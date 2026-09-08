/* Synthetic audio only: exercise the actual mixer and translated memory reads. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "../kernel/xk_audio.c"

static uint8_t ram[16 * 4096];
static uint32_t pages[16];
uint8_t *g_xram = ram;
uint32_t *g_xpt = pages;
static jmp_buf sink_done;
static unsigned writes;
static const int16_t *pending;
static int16_t submitted[XA_GRAIN * 2];

uint64_t xk_os_monotonic_us(void) { return 1000000; }
void xk_os_log(const char *fmt, ...) { (void)fmt; }
void xk_os_audio_mutex_lock(void) {}
void xk_os_audio_mutex_unlock(void) {}
int xk_os_audio_open(int rate, int grain) { return 0; }
int xk_os_audio_thread_start(void (*fn)(void *), void *arg) { return 0; }
void xk_os_audio_write(const int16_t *samples, int frames)
{
    assert(frames == XA_GRAIN);
    /* The previous submission remains owned by the sink until this call. */
    if (pending) {
        assert(samples != pending);
        assert(!memcmp(pending, submitted, sizeof submitted));
    }
    assert(((uintptr_t)samples & 63u) == 0);
    memcpy(submitted, samples, sizeof submitted);
    pending = samples;
    if (++writes == 8) longjmp(sink_done, 1);
}

static void init_memory(void)
{
    memset(ram, 0xa5, sizeof ram);
    for (unsigned i = 0; i < 16; ++i) pages[i] = i * 4096;
    /* Adjacent virtual pages backed by deliberately separated host pages. */
    pages[9] = 12 * 4096;
    pages[10] = 14 * 4096;
}

static void format(unsigned channels, unsigned bits, unsigned adpcm)
{
    X_M16(128) = adpcm ? 0x69 : 1;
    X_M16(130) = channels;
    X_M32(132) = 22050;
    X_M16(140) = adpcm ? 36 * channels : channels * bits / 8;
    X_M16(142) = bits;
}

static void page_reads(void)
{
    uint8_t encoded[576];
    int16_t expected[64 * 2];
    for (unsigned adpcm = 0; adpcm < 2; ++adpcm)
    for (unsigned ch = 1; ch <= 2; ++ch)
    for (unsigned bits = 8; bits <= 16; bits += 8) {
        unsigned block = adpcm ? ch * 36 : ch * bits / 8 * 64;
        for (unsigned i = 0; i < sizeof encoded; ++i) encoded[i] = (i * 37u + 19u) & 255;
        if (adpcm) for (unsigned b = 0; b < 2; ++b)
            for (unsigned c = 0; c < ch; ++c) {
                encoded[b * block + c * 4 + 2] = 40;
                encoded[b * block + c * 4 + 3] = 0;
            }
        for (unsigned offset = 1; offset < block; ++offset)
        for (int kind = 1; kind <= 2; ++kind) {
            init_memory(); format(ch, bits, adpcm);
            unsigned addr = 9 * 4096 - offset;
            x_guest_write(2048, encoded, 2 * block);
            x_guest_write(addr, encoded, 2 * block);
            xa_voice reference = {0}, fragmented = {0};
            voice_parse_wfx(&reference, 128); reference.kind = 1;
            reference.data = 2048; reference.size = 2 * block;
            fragmented = reference; fragmented.kind = kind; fragmented.data = addr;
            if (kind == 2) {
                /* A packet boundary and a guest-page boundary in the same block. */
                unsigned cut = offset / 2 + 1;
                fragmented.nq = 2;
                fragmented.q[0] = (xa_pkt){addr, cut, 0};
                fragmented.q[1] = (xa_pkt){addr + cut, 2 * block - cut, 0};
            }
            for (unsigned b = 0; b < 2; ++b) {
                assert(voice_refill(&reference));
                unsigned count = reference.blk_frames * ch;
                memcpy(expected, reference.blk, count * sizeof *expected);
                unsigned got = 0;
                while (got < count) {
                    assert(voice_refill(&fragmented));
                    unsigned n = fragmented.blk_frames * ch;
                    assert(got + n <= count);
                    assert(!memcmp(expected + got, fragmented.blk, n * sizeof *expected));
                    got += n;
                }
            }
        }
    }
    puts("audio: PCM8/16 and mono/stereo ADPCM agree across every page/packet split");
}

static void sink_lifetime(void)
{
    init_memory(); format(1, 16, 0);
    for (unsigned i = 0; i < 8192; ++i) X_M16(4096 + i * 2) = (i * 31u) % 20000;
    memset(g_v, 0, sizeof g_v);
    xa_voice *v = &g_v[0]; voice_parse_wfx(v, 128);
    v->used = v->playing = v->looping = 1; v->kind = 1; v->volume = 1;
    v->data = 4096; v->size = 16384;
    if (!setjmp(sink_done)) mixer_thread(NULL);
    assert(writes == 8);
    puts("audio: queued output remains immutable while the next grain is mixed");
}

int main(int argc, char **argv)
{
    if (argc == 1 || !strcmp(argv[1], "pages")) page_reads();
    if (argc == 1 || !strcmp(argv[1], "sink")) sink_lifetime();
    return 0;
}
