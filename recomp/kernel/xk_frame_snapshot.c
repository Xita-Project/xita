#include "xk_frame_snapshot.h"
#include <string.h>

enum { EMPTY, WRITING, READY, READING, NONE=2 };
_Static_assert(__atomic_always_lock_free(sizeof(unsigned), 0),
               "snapshot ownership requires lock-free unsigned atomics");

int xv_frame_snapshot_init(xv_frame_snapshot *q,void *a,void *b,size_t capacity)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!q || !a || !b || !capacity || capacity>UINTPTR_MAX-x ||
       capacity>UINTPTR_MAX-y || (x<y+capacity && y<x+capacity))return 0;
    memset(q,0,sizeof *q);q->slots[0].data=a;q->slots[1].data=b;
    q->capacity=capacity;q->published=q->writing=q->reading=NONE;
    return 1;
}
void *xv_frame_snapshot_begin(xv_frame_snapshot *q)
{
    if(q->writing!=NONE || q->next_generation==UINT64_MAX)return NULL;
    unsigned current=__atomic_load_n(&q->published,__ATOMIC_ACQUIRE);
    unsigned slot=current==NONE?0:current^1u;
    unsigned old=__atomic_load_n(&q->slots[slot].state,__ATOMIC_ACQUIRE);
    if(old!=EMPTY && old!=READY)return NULL;
    if(!__atomic_compare_exchange_n(&q->slots[slot].state,&old,WRITING,0,
                                   __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return NULL;
    q->writing=slot;return q->slots[slot].data;
}
int xv_frame_snapshot_publish(xv_frame_snapshot *q,size_t size)
{
    if(q->writing==NONE || size>q->capacity)return 0;
    unsigned slot=q->writing;
    q->slots[slot].size=size;q->slots[slot].generation=++q->next_generation;
    __atomic_store_n(&q->slots[slot].state,READY,__ATOMIC_RELEASE);
    __atomic_store_n(&q->published,slot,__ATOMIC_RELEASE);
    q->writing=NONE;return 1;
}
void xv_frame_snapshot_cancel(xv_frame_snapshot *q)
{
    if(q->writing==NONE)return;
    __atomic_store_n(&q->slots[q->writing].state,EMPTY,__ATOMIC_RELEASE);
    q->writing=NONE;
}
int xv_frame_snapshot_acquire(xv_frame_snapshot *q,xv_snapshot_view *out)
{
    if(!out || q->reading!=NONE)return 0;
    /* Bounded retries: this is allowed to decline when publication races. */
    for(unsigned attempt=0;attempt<2;attempt++) {
        unsigned slot=__atomic_load_n(&q->published,__ATOMIC_ACQUIRE);
        if(slot==NONE)return 0;
        unsigned expected=READY;
        if(!__atomic_compare_exchange_n(&q->slots[slot].state,&expected,READING,0,
                                       __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))continue;
        if(slot!=__atomic_load_n(&q->published,__ATOMIC_ACQUIRE)) {
            __atomic_store_n(&q->slots[slot].state,READY,__ATOMIC_RELEASE);continue;
        }
        q->reading=slot;
        out->data=q->slots[slot].data;out->size=q->slots[slot].size;
        out->generation=q->slots[slot].generation;return 1;
    }
    return 0;
}
void xv_frame_snapshot_release(xv_frame_snapshot *q)
{
    if(q->reading==NONE)return;
    __atomic_store_n(&q->slots[q->reading].state,READY,__ATOMIC_RELEASE);
    q->reading=NONE;
}
