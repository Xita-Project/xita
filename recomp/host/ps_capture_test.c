#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../runtime/xv_ps_capture.h"

static xv_ps_capture candidate;
static uint32_t original[512];
static unsigned count;

static void check(uint32_t hash)
{
    int expected = 0;
    if (count < 512) {
        unsigned i;
        for (i = 0; i < count && original[i] != hash; i++) {}
        if (i == count) { original[count++] = hash; expected = 1; }
    }
    assert(xv_ps_capture_insert(&candidate, hash) == expected);
    assert(candidate.count == count);
}

static void reset(void)
{
    memset(&candidate, 0, sizeof candidate);
    count = 0;
}

int main(void)
{
    reset();
    check(0); check(0); check(UINT32_MAX); check(UINT32_MAX);
    /* Mix repetitions and new values against the original logging policy. */
    uint32_t random = 12345;
    for (unsigned i = 0; i < 100000; i++) {
        random = random * 1664525u + 1013904223u;
        check(i & 1 ? random & 127u : random);
    }
    assert(count == 512);
    /* Force every insertion into one bucket, including wraparound probing. */
    reset();
    for (uint32_t key = 0; count < 512; key++) {
        if (((key * 2654435761u) >> 22) != 1023) continue;
        check(key); check(key);
    }
    xv_ps_capture full = candidate;
    for (unsigned i = 0; i < 1024; i++) check(i);
    assert(!memcmp(&full, &candidate, sizeof full));
    puts("PASS: shader capture policy, duplicate/zero hashes, collisions, wrap and full-table retirement");
}
