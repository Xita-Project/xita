#define XV_PACKED_VERTEX_LAYOUT 1
#define XV_VERTEX_PREPARE_TEST_NO_MAIN
#include "vertex_prepare_test.c"
int main(void)
{
    uint8_t *source=malloc(131072),*saved=malloc(131072);assert(source && saved);
    for(unsigned i=0;i<131072;i++)source[i]=(uint8_t)(i*17+i/32);
    xv_vertex_upload_override(1);xv_vertex_worker_override(1);
    for(unsigned native=0;native<2;native++) {
        xv_vertex_prepare_override(native,65536);
        for(unsigned frame=0;frame<9;frame++) {
            unsigned slot=frame%3;xv_vertex_upload_reset(slot);source[frame*32]^=71;
            memcpy(saved,source,131072);
            xv_vertex_prepare_batch b={.slot=slot,.count=2,.streams={
                {.source=source,.bytes=131072,.stride=32,.packed=XV_PACKED_PREFIX16},
                {.source=source,.bytes=131072,.stride=32}}};
            __atomic_store_n(&pause_prepare,native,__ATOMIC_RELEASE);
            xv_vertex_prepare_begin(&b);
            assert(!!pending==native);
            if(native) {
                while(!__atomic_load_n(&parked,__ATOMIC_ACQUIRE))usleep(100);
                assert(!__atomic_load_n(&complete,__ATOMIC_ACQUIRE));
                __atomic_store_n(&pause_prepare,0,__ATOMIC_RELEASE);
            }
            assert(xv_vertex_prepare_finish(&b));
            memset(source,0xff,131072); /* source loan really ended */
            xv_vertex_upload_seal(slot);xv_vertex_upload_wait(slot);
            assert(!memcmp(b.streams[1].result,saved,131072));
            for(unsigned i=0;i<4096;i++)
                assert(!memcmp((const uint8_t *)b.streams[0].result+i*16,saved+i*32,16));
        }
        reset();
    }
    xv_vertex_prepare_override(0,1);
    for(unsigned bad=0;bad<3;bad++) {
        xv_vertex_prepare_batch b={.count=1,.streams={{.source=source,.bytes=32,.stride=32,.packed=1}}};
        if(!bad)b.streams[0].stride=16;
        else if(bad==1)b.streams[0].packed=2;
        else b.streams[0].bytes=33;
        xv_vertex_prepare_begin(&b);assert(!xv_vertex_prepare_finish(&b));
    }
    reset();free(source);free(saved);
    puts("PASS actual packed prepare: inline/native source loans, delayed prepare, mixed raw/packed batch, copy-worker join, 18 slot generations and malformed declines");
}
