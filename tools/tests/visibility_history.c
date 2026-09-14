#define XV_FLARE_QUERY_OVERLAP 1
#include "runtime/xv_visibility.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static xv_visibility_result results[XV_VISIBILITY_IDS];
static unsigned start, finished, observations;
static uint32_t color(uint32_t serial) { return serial ^ 0x5a319e87u; }
static void *publisher(void *unused)
{
    (void)unused;
    while (!__atomic_load_n(&start,__ATOMIC_ACQUIRE)) {}
    for (uint32_t serial=1;serial<=500000;serial++) {
        xv_visibility_publish(results,0,serial,color(serial));
        if (serial==1) while (!__atomic_load_n(&observations,__ATOMIC_ACQUIRE)) {}
    }
    __atomic_store_n(&finished,1,__ATOMIC_RELEASE);
    return NULL;
}
int main(void)
{
    uint16_t slot;uint32_t serial,pixels=0x12345678;
    assert(xv_visibility_read_generation(results,0,1,&pixels)==XV_VISIBILITY_INVALID_ARGUMENT);
    assert(!xv_visibility_generation(results,0));
    assert(!xv_visibility_issue(results,0,&slot,&serial) && slot==0 && serial==1);
    assert(xv_visibility_read_generation(results,0,1,&pixels)==XV_VISIBILITY_INCOMPLETE && pixels==0x12345678);
    for (unsigned i=1;i<=4;i++) {
        if(i>1)assert(!xv_visibility_issue(results,0,&slot,&serial) && serial==i);
        xv_visibility_publish(results,slot,serial,color(serial));
    }
    for(unsigned i=1;i<=4;i++)assert(!xv_visibility_read_generation(results,0,i,&pixels)&&pixels==color(i));
    assert(!xv_visibility_issue(results,0,&slot,&serial) && serial==5);
    /* Recording a replacement preserves the old completed generation. */
    assert(xv_visibility_read(results,0,&pixels)==XV_VISIBILITY_INCOMPLETE);
    assert(!xv_visibility_read_generation(results,0,1,&pixels)&&pixels==color(1));
    xv_visibility_publish(results,slot,serial,color(serial));
    pixels=0x12345678;
    assert(xv_visibility_read_generation(results,0,1,&pixels)==XV_VISIBILITY_INCOMPLETE && pixels==0x12345678);
    assert(!xv_visibility_read_generation(results,0,5,&pixels)&&pixels==color(5));
    /* Arbitrary IDs, serial wrap, sequence wrap, and an interrupted publish. */
    assert(!xv_visibility_issue(results,12345,&slot,&serial));
    results[slot].issued=UINT32_MAX-1u;
    assert(!xv_visibility_issue(results,12345,&slot,&serial)&&serial==UINT32_MAX);
    results[slot].history[serial%4].sequence=UINT32_MAX-1u;
    xv_visibility_publish(results,slot,serial,color(serial));
    assert(!xv_visibility_read_generation(results,12345,serial,&pixels)&&pixels==color(serial));
    assert(!xv_visibility_issue(results,12345,&slot,&serial)&&serial==1);
    assert(!xv_visibility_read_generation(results,12345,UINT32_MAX,&pixels));
    results[slot].history[1].sequence=1;
    pixels=0x12345678;
    assert(xv_visibility_read_generation(results,12345,1,&pixels)==XV_VISIBILITY_INCOMPLETE&&pixels==0x12345678);
    assert(xv_visibility_read_generation(results,12345,0,&pixels)==XV_VISIBILITY_INVALID_ARGUMENT);
    pthread_t thread;assert(!pthread_create(&thread,NULL,publisher,NULL));
    __atomic_store_n(&start,1,__ATOMIC_RELEASE);
    do {
        uint32_t current=__atomic_load_n(&results[0].completed,__ATOMIC_ACQUIRE);
        for(unsigned age=0;age<8 && current>age;age++) {
            uint32_t target=current-age,value=0x12345678;
            uint32_t rc=xv_visibility_read_generation(results,0,target,&value);
            assert(rc==0 || rc==XV_VISIBILITY_INCOMPLETE);
            assert(rc ? value==0x12345678 : value==color(target));
            if(!rc)__atomic_add_fetch(&observations,1,__ATOMIC_RELEASE);
        }
    } while(!__atomic_load_n(&finished,__ATOMIC_ACQUIRE));
    assert(!pthread_join(thread,NULL));
    assert(observations);
    printf("PASS: exact visibility generations, ID/serial/sequence wrap, overwritten history, and 500000 concurrent publications (%u verified reads)\n",observations);
}
