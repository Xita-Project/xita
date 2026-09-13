#include "push_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    uint32_t words[32], fault_address, reject_address, reads, count;
    struct { uint32_t value, address; uint16_t method; uint8_t subchannel; } output[64];
} fixture;
static int read_word(void *opaque, uint32_t address, uint32_t *word)
{
    fixture *f = opaque; ++f->reads;
    assert(address >= 0x1000 && address < 0x1080 && !(address & 3));
    if (address == f->fault_address) return 0;
    *word = f->words[(address - 0x1000) / 4];
    return 1;
}
static int emit(void *opaque, uint8_t subchannel, uint16_t method,
                uint32_t value, uint32_t address)
{
    fixture *f = opaque;
    if (address == f->reject_address) return 0;
    assert(f->count < 64);
    f->output[f->count].value = value; f->output[f->count].address = address;
    f->output[f->count].method = method; f->output[f->count].subchannel = subchannel;
    ++f->count;
    return 1;
}
static uint32_t packet(unsigned count, unsigned subchannel, unsigned method, int ni)
{ return (count << 18) | (subchannel << 13) | method | (ni ? 0x40000000u : 0); }
static enum h2_push_result run(h2_push_stream *s, uint32_t put, uint32_t budget, fixture *f, h2_push_fault *fault)
{ return h2_push_run(s, put, budget, read_word, emit, f, fault); }
int main(void)
{
    h2_push_stream s, before; fixture f = {0}; h2_push_fault fault;
    assert(h2_push_init(&s, 0x1000, sizeof f.words)); before = s;
    assert(!h2_push_init(&s, 1, 4) && !h2_push_init(&s, 0, 0));
    assert(!h2_push_init(&s, 0xFFFFFFFC, 4) && !h2_push_init(NULL, 0, 4));
    assert(memcmp(&s, &before, sizeof s) == 0);
    assert(run(&s, 0x1000, 0, &f, &fault) == H2_PUSH_COMPLETE && !f.reads);
    /* Increasing, non-increasing, zero-count and raw data resembling commands. */
    f.words[0] = packet(3, 6, 0x100, 0);
    f.words[1] = 0xFFFFFFFF; f.words[2] = 0x00020000; f.words[3] = 0x1041;
    f.words[4] = packet(2, 7, 0x1800, 1);
    f.words[5] = 0x80000000; f.words[6] = 0xDEADBEEF; f.words[7] = 0;
    assert(run(&s, 0x1020, 20, &f, &fault) == H2_PUSH_COMPLETE && f.count == 5);
    assert(f.output[0].subchannel == 6 && f.output[0].method == 0x100 && f.output[0].value == 0xFFFFFFFF);
    assert(f.output[1].method == 0x104 && f.output[1].value == 0x20000);
    assert(f.output[2].method == 0x108 && f.output[2].address == 0x100C);
    assert(f.output[3].subchannel == 7 && f.output[3].method == 0x1800 && f.output[4].method == 0x1800);
    /* A method packet may span several PUT updates without replaying a value. */
    h2_push_init(&s, 0x1000, sizeof f.words); f.count = 0;
    assert(run(&s, 0x1004, 1, &f, &fault) == H2_PUSH_NEED_DATA && !f.count && s.remaining == 3);
    assert(run(&s, 0x100C, 20, &f, &fault) == H2_PUSH_NEED_DATA && f.count == 2 && s.remaining == 1);
    assert(run(&s, 0x1010, 20, &f, &fault) == H2_PUSH_COMPLETE && f.count == 3);
    /* Rejection retains the rejected data word and all parser state. */
    h2_push_init(&s, 0x1000, sizeof f.words); f.count = 0;
    assert(run(&s, 0x1010, 1, &f, &fault) == H2_PUSH_BUDGET_EXHAUSTED);
    before = s; f.reject_address = 0x1004;
    assert(run(&s, 0x1010, 10, &f, &fault) == H2_PUSH_METHOD_REJECTED);
    assert(!f.count && fault.address == 0x1004 && fault.word == 0xFFFFFFFF && fault.method == 0x100 && fault.subchannel == 6);
    assert(memcmp(&s, &before, sizeof s) == 0);
    f.reject_address = 0;
    assert(run(&s, 0x1010, 10, &f, &fault) == H2_PUSH_COMPLETE && f.count == 3);
    /* Both jump forms, including the observed ring-wrap form (base | 1). */
    memset(&f, 0, sizeof f); h2_push_init(&s, 0x1000, sizeof f.words);
    s.get = 0x107C; f.words[31] = 0x1001;
    f.words[0] = 0x20001010; f.words[4] = packet(1, 0, 0, 0); f.words[5] = 0x97;
    assert(run(&s, 0x1018, 10, &f, &fault) == H2_PUSH_COMPLETE);
    assert(f.count == 1 && f.output[0].method == 0 && f.output[0].value == 0x97);
    /* Cycle budget leaves a repeatable checkpoint; there is no unbounded loop. */
    h2_push_init(&s, 0x1000, sizeof f.words); f.words[0] = 0x1001; f.reads = 0;
    assert(run(&s, 0x1004, 7, &f, &fault) == H2_PUSH_BUDGET_EXHAUSTED && f.reads == 7 && s.get == 0x1000);
    /* No implicit ring wrap and no reads beyond the owned allocation. */
    s.get = 0x1080; f.reads = 0;
    assert(run(&s, 0x1000, 1, &f, &fault) == H2_PUSH_MEMORY_FAULT && !f.reads);
    h2_push_init(&s, 0x1000, sizeof f.words); f.words[0] = 0x20001080;
    assert(run(&s, 0x1080, 1, &f, &fault) == H2_PUSH_COMPLETE);
    const uint32_t bad[] = {0x00020000, 0x1042, 0x80000000, 0x00010000, 0x00030000, 3};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) {
        h2_push_init(&s, 0x1000, sizeof f.words); before = s; f.words[0] = bad[i];
        assert(run(&s, 0x1004, 1, &f, &fault) == H2_PUSH_UNSUPPORTED_COMMAND);
        assert(fault.word == bad[i] && memcmp(&s, &before, sizeof s) == 0);
    }
    f.words[0] = 0x1085;
    assert(run(&s, 0x1004, 1, &f, &fault) == H2_PUSH_MEMORY_FAULT && s.get == 0x1000);
    f.fault_address = 0x1000;
    assert(run(&s, 0x1004, 1, &f, &fault) == H2_PUSH_MEMORY_FAULT && fault.word == 0);
    f.fault_address = 0;
    assert(run(&s, 0x1084, 1, &f, &fault) == H2_PUSH_BAD_STATE);
    assert(run(&s, 0x1001, 1, &f, &fault) == H2_PUSH_BAD_STATE);
    /* The documented 11-bit method index wraps; policy belongs to the consumer. */
    f.count = 0; f.words[0] = packet(2, 0, 0x1FFC, 0); f.words[1] = 5; f.words[2] = 6;
    assert(run(&s, 0x100C, 3, &f, &fault) == H2_PUSH_COMPLETE);
    assert(f.output[0].method == 0x1FFC && f.output[1].method == 0);
    puts("Halo 2 push parser: packet lanes, split PUT, rejection, jumps, ring bounds and cycle budget passed");
    return 0;
}
