/* Reporting only: owned by the game thread, never read by the audio mixer. */
#pragma once
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t size, rate, frequency, align, tag, channels, bits;
    uint64_t start_us, end_us, duration_us, offset_us;
    int stopped, paused, looping, last_report;
} ds_state;

static uint32_t ds_state_rate(const ds_state *s)
{ return s->frequency ? s->frequency : s->rate; }
static uint64_t ds_state_duration(const ds_state *s, uint32_t bytes)
{
    uint64_t frames = s->tag == 0x69 ? (uint64_t)bytes * 64 : bytes;
    uint64_t denom = (uint64_t)s->align * ds_state_rate(s);
    return denom ? (frames * 1000000 + denom - 1) / denom : 0;
}
static int ds_state_playing(const ds_state *s, uint64_t now)
{ return !s->stopped && !s->paused && (s->looping || now < s->end_us); }
static void ds_state_play(ds_state *s, uint64_t now, int looping)
{
    s->duration_us = ds_state_duration(s, s->size);
    if (s->offset_us >= s->duration_us) s->offset_us = 0;
    s->start_us = now; s->end_us = now + s->duration_us - s->offset_us;
    s->stopped = s->paused = 0; s->looping = looping;
}
static void ds_state_stop(ds_state *s, uint64_t now)
{
    if (ds_state_playing(s, now)) {
        s->offset_us += now - s->start_us;
        if (s->looping && s->duration_us) s->offset_us %= s->duration_us;
    }
    if (!s->stopped && !s->paused && !s->looping && now >= s->end_us)
        s->offset_us = s->duration_us;
    s->stopped = 1;
}
/* Preserve elapsed source position across a frequency/format change. */
static void ds_state_retime(ds_state *s, uint64_t now, uint64_t old_duration)
{
    uint64_t offset = s->offset_us;
    if (!s->stopped && !s->paused) offset += now - s->start_us;
    if (s->looping && old_duration) offset %= old_duration;
    if (offset > old_duration) offset = old_duration;
    s->duration_us = ds_state_duration(s, s->size);
    s->offset_us = old_duration ? (uint64_t)((double)offset * s->duration_us / old_duration) : 0;
    s->start_us = now; s->end_us = now + s->duration_us - s->offset_us;
}
