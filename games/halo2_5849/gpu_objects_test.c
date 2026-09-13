#include "gpu_objects.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture { uint32_t words[0x5000 / 4], fault; } fixture;
static int read_word(void *opaque, uint32_t offset, uint32_t *word)
{
    fixture *f = opaque;
    if ((offset & 3) || offset < 0x10000 || offset >= 0x15000 || offset == f->fault) return 0;
    *word = f->words[(offset - 0x10000) / 4]; return 1;
}
static void store(fixture *f, uint32_t offset, uint32_t value)
{ assert(offset >= 0x10000 && offset < 0x15000); f->words[(offset - 0x10000) / 4] = value; }
int main(void)
{
    fixture f = {0}; h2_gpu_object obj = {0}, before;
    store(&f, 0x10068, 13); store(&f, 0x1006C, 0x800114AB);
    assert(h2_object_lookup(read_word, &f, 13, 0, &obj));
    assert(obj.instance == 0x14AB0 && obj.engine == 1 && obj.channel == 0); before = obj;
    assert(!h2_object_lookup(read_word, &f, 12, 0, &obj));
    assert(!h2_object_lookup(read_word, &f, 13, 1, &obj));
    assert(memcmp(&obj, &before, sizeof obj) == 0);
    /* Another channel may reuse a handle; duplicate same-channel handles reject. */
    store(&f, 0x10070, 13); store(&f, 0x10074, 0x810014AE);
    assert(h2_object_lookup(read_word, &f, 13, 1, &obj) && obj.instance == 0x14AE0 && obj.engine == 0);
    store(&f, 0x10074, 0x800014AE); before = obj;
    assert(!h2_object_lookup(read_word, &f, 13, 0, &obj) && memcmp(&obj, &before, sizeof obj) == 0);
    store(&f, 0x10074, 0); f.fault = 0x1006C;
    assert(!h2_object_lookup(read_word, &f, 13, 0, &obj)); f.fault = 0;
    store(&f, 0x1006C, 0x804114AB);
    assert(!h2_object_lookup(read_word, &f, 13, 0, &obj));
    /* Driver-style linear descriptor with nonzero first-page byte adjustment. */
    store(&f, 0x11120, 0x2342B03D); store(&f, 0x11124, 31);
    store(&f, 0x11128, 0x03D00237); store(&f, 0x1112C, 0x03D00237);
    h2_dma_object dma, saved; assert(h2_dma_load(read_word, &f, 0x11120, &dma));
    assert(dma.address == 0x03D00234 && dma.limit == 31 && dma.object_class == 0x3D && dma.target == 2);
    uint32_t address = 0xDEADBEEF;
    assert(h2_dma_resolve(&dma, 0, 32, 1, 0x4000000, &address) && address == 0x03D00234);
    assert(h2_dma_resolve(&dma, 31, 1, 0, 0x4000000, &address) && address == 0x03D00253);
    address = 0xDEADBEEF;
    assert(!h2_dma_resolve(&dma, 32, 1, 0, 0x4000000, &address));
    assert(!h2_dma_resolve(&dma, 0, 33, 1, 0x4000000, &address));
    assert(!h2_dma_resolve(&dma, 0, 0, 1, 0x4000000, &address));
    assert(address == 0xDEADBEEF);
    for (unsigned cls = 2; cls <= 3; ++cls) {
        store(&f, 0x11120, 0xB000 | cls); assert(h2_dma_load(read_word, &f, 0x11120, &dma));
        assert(h2_dma_resolve(&dma, 0, 4, cls == 3, 0x4000000, &address));
        assert(!h2_dma_resolve(&dma, 0, 4, cls != 3, 0x4000000, &address));
    }
    /* Wider DMA limits do not create extra physical memory or permit wrapping. */
    dma.object_class = 0x3D; dma.address = 0; dma.limit = 0x7FFAFFF;
    assert(h2_dma_resolve(&dma, 0x3FFFFFF, 1, 1, 0x4000000, &address));
    assert(!h2_dma_resolve(&dma, 0x4000000, 1, 1, 0x4000000, &address));
    dma.address = 0xFFFFFFFC; dma.limit = 0xFFFFFFFF;
    assert(!h2_dma_resolve(&dma, 8, 4, 0, 0x4000000, &address));
    dma.address = 0;
    assert(!h2_dma_resolve(&dma, 0xFFFFFFFF, 2, 0, 0x4000000, &address));
    saved = dma;
    const uint32_t bad_flags[] = {0xB097, 0xA03D, 0xF03D, 0x1B03D, 0x3B03D, 0x8B03D};
    for (unsigned i = 0; i < sizeof bad_flags / sizeof *bad_flags; ++i) {
        store(&f, 0x11120, bad_flags[i]);
        assert(!h2_dma_load(read_word, &f, 0x11120, &dma));
        assert(memcmp(&dma, &saved, sizeof dma) == 0);
    }
    store(&f, 0x11120, 0xB03D); store(&f, 0x1112C, 0x03D01003);
    assert(!h2_dma_load(read_word, &f, 0x11120, &dma));
    store(&f, 0x1112C, 0x03D00001); store(&f, 0x11128, 0x03D00001);
    assert(!h2_dma_load(read_word, &f, 0x11120, &dma));
    assert(!h2_dma_load(read_word, &f, 0x11121, &dma));
    assert(!h2_dma_load(read_word, &f, 0x15000, &dma));
    assert(!h2_dma_load(read_word, &f, 0xFFFFFFFF, &dma));
    puts("Halo 2 GPU objects: unique channel bindings, linear DMA permissions, inclusive limits and physical bounds passed");
    return 0;
}
