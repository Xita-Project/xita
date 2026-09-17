#pragma once
#include <stdint.h>
/* Read-only inventory under the real mixer lock. Only these seven voices may
 * play: two muted PCM owners, four checked zero streams and one late movie. */
/* allowed: per-voice flags of registered game stream voices that may also play (NULL = none). */
int h2_audio_movie_contract(int movie,const int muted[2],const int zero[4],int movie_playing,const uint8_t *allowed);
