#include <assert.h>
#include <stdio.h>
#include "../../runtime/xv_frame_pacer.h"
int main(void)
{
    uint64_t next = 0;
    assert(xv_frame_pacer_wait(1000, &next, 50000) == 0);
    for (unsigned i = 1; i <= 400; i++) {
        uint64_t start = 1000 + (uint64_t)i * 50000;
        assert(xv_frame_pacer_wait(start - 17000, &next, 50000) == 17000);
        assert(next == start + 50000);
    }
    /* A slow frame or pause must not cause multiple frames to be presented at once. */
    uint64_t late = next + 900000;
    assert(xv_frame_pacer_wait(late, &next, 50000) == 0);
    assert(xv_frame_pacer_wait(late + 1000, &next, 50000) == 49000);
    assert(xv_frame_pacer_wait(late, &next, 0) == 0 && next == 0);
    puts("PASS: 20 fps deadlines, late-frame recovery, cap disabled");
}
