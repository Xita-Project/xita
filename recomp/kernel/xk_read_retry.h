/* Complete short positional reads through temporary storage. Normal reads keep
 * their direct path. Vita3K can return a short host read when its GPU tracking
 * protects part of the destination; a CPU copy triggers the tracking handler.
 * Never fill missing file data here: EOF/errors leave the unread bytes intact.
 */
#ifndef XK_READ_RETRY_H
#define XK_READ_RETRY_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef int64_t (*xk_read_once_fn)(void *, uint64_t, void *, uint32_t);

static inline int64_t xk_read_retry(void *file, uint64_t pos, void *dst,
                                  uint32_t size, xk_read_once_fn read_once,
                                  unsigned *retries)
{
    *retries = 0;
    int64_t first = read_once(file, pos, dst, size);
    if (first < 0 || (uint64_t)first >= size) return first;
    const uint32_t capacity = 32768;
    uint8_t *scratch = malloc(capacity);
    if (!scratch) return first;
    uint32_t done = (uint32_t)first;
    while (done < size) {
        uint32_t count = size - done;
        if (count > capacity) count = capacity;
        ++*retries;
        int64_t part = read_once(file, pos + done, scratch, count);
        if (part <= 0) {
            free(scratch);
            return done ? (int64_t)done : part;
        }
        memcpy((uint8_t *)dst + done, scratch, (size_t)part);
        done += (uint32_t)part;
    }
    free(scratch);
    return done;
}
#endif
