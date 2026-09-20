#pragma once
#include <stdlib.h>

/* One publisher, serialized by an enclosing lock. Helpers may only execute
 * the bounded pure callback. Arguments/results stay alive through retire.
 * 0 idle, 1 offered, 2 claimed, 3 complete. No guest service queue is involved. */
typedef struct {
    unsigned state;
    void (*run)(void *);
    void *argument;
} xv_captured_task;

static inline void xv_captured_offer(xv_captured_task *task,
    void (*run)(void *),void *argument)
{
    if(__atomic_load_n(&task->state,__ATOMIC_ACQUIRE)!=0||!run)abort();
    task->run=run;task->argument=argument;
    __atomic_store_n(&task->state,1,__ATOMIC_RELEASE);
}
static inline int xv_captured_try_run(xv_captured_task *task)
{
    unsigned expected=1;
    if(!__atomic_compare_exchange_n(&task->state,&expected,2,0,
        __ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
    task->run(task->argument);
    /* Do not touch task or argument after publishing completion. */
    __atomic_store_n(&task->state,3,__ATOMIC_RELEASE);
    return 1;
}
/* Only the publisher retires; an acquire observes all result writes. */
static inline int xv_captured_retire(xv_captured_task *task)
{
    if(__atomic_load_n(&task->state,__ATOMIC_ACQUIRE)!=3)return 0;
    __atomic_store_n(&task->state,0,__ATOMIC_RELEASE);
    return 1;
}
