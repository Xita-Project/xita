#pragma once
#include <stdint.h>
/* Read-only inventory under the real mixer lock. Only these seven voices may
 * play: two muted PCM owners, four checked zero streams and one late movie. */
int h2_audio_movie_contract(int movie,const int muted[2],const int zero[4],int movie_playing);
