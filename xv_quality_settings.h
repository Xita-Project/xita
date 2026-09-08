#ifndef XV_QUALITY_SETTINGS_H
#define XV_QUALITY_SETTINGS_H

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

/* Startup settings only. Invalid values retain the existing rendering path. */
static inline int xv_quality_int(const char *name, int fallback, int lo, int hi)
{
    const char *s = getenv(name);
    if (!s || !*s) return fallback;
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || errno) return fallback;
    while (isspace((unsigned char)*end)) ++end;
    if ((*end && *end != '#' && *end != ';') || v < lo || v > hi) return fallback;
    return (int)v;
}

static inline unsigned xv_render_width(unsigned height)
{
    switch (height) {
    case 360: return 640;
    case 400: return 704;
    case 480: return 848;
    default: return 960;
    }
}

#endif
