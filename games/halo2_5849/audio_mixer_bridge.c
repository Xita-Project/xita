/* Same shared decoder, compiled only for the H2 diagnostic target. Keeping
 * the read-only cursor probe in this translation unit avoids exporting shared
 * mutable voice state or changing the CE mixer. */
#include "recomp/kernel/xk_audio.c"
#include "audio_stream_cursor.h"
int h2_audio_stream_cursor_read(int voice, h2_stream_cursor *out)
{
    if (voice<0 || voice>=XA_MAX_VOICES || !out) return 0;
    xk_audio_lock();int valid=h2_stream_cursor_from_voice(&g_v[voice],out);xk_audio_unlock();
    return valid;
}
