/* Experimental captured marker record. No hook enables this by default. */
#if XV_NATIVE_MARKER_RECORD
#include "xk.h"
#include "xk_object_jobs.h"
#include "xk_marker_snapshot.h"
#include <stdlib.h>

extern int xv_object_marker_admit(xctx *,int);
extern void xv_object_marker_captured(int);
static int selected=-1;
static int enabled(void)
{
    int value=__atomic_load_n(&selected,__ATOMIC_ACQUIRE);
    if(value<0) {
        const char *option=getenv("XV_NATIVE_MARKER_RECORD");
        const char *math=getenv("XV_NATIVE_MATH");
        value=(!option||atoi(option)!=0)&&(!math||atoi(math)!=0);
        __atomic_store_n(&selected,value,__ATOMIC_RELEASE);
    }
    return value;
}
static void *span(uint32_t address,unsigned bytes)
{
    if((address&3u)||bytes>4096u-(address&4095u))return NULL;
    return X_G(address);
}
static int overlap(const void *a,unsigned an,const void *b,unsigned bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<y+bn&&y<x+an;
}
int xv_math_marker_record(xctx *c)
{
    if(!enabled())return 0;
    int guard __attribute__((cleanup(xv_object_math_unlock)))=xv_object_math_lock();
    if(!xv_object_marker_admit(c,guard))return 0;
    uint32_t sp=c->r[4],source=c->r[7],destination=c->r[6];
    if(source>UINT32_MAX-32u)return 0;
    uint32_t *stack=span(sp-32u,72);
    uint8_t *output=span(destination,108);
    const uint8_t *record=span(source+4u,28);
    const uint8_t *constants=span(0x1f0a68u,0xa0u);
    if(!stack||!output||!record||!constants)return 0;
    xv_marker_input input;
    input.matrix_base=stack[17]; /* original [sp+24h] */
    int64_t matrix_address=(int64_t)input.matrix_base+(int16_t)c->r[0]*52;
    if(matrix_address<0||matrix_address>UINT32_MAX-52u)return 0;
    const float *matrix=span((uint32_t)matrix_address,52);
    if(!matrix)return 0;
    if(overlap(output,108,stack,72)||
       overlap(output,108,record,28)||overlap(stack,32,record,28)||
       overlap(output,108,matrix,52)||overlap(stack,32,matrix,52)||
       overlap(output,108,constants,0xa0)||overlap(stack,32,constants,0xa0))return 0;
    /* Capture every shared read before releasing the original transaction.
     * Neither borrowed source pointer is used by computation or publication. */
    memcpy(input.translation,record,12);memcpy(input.quaternion,record+12,16);
    memcpy(input.matrix,matrix,52);
    memcpy(&input.zero,constants,4);memcpy(&input.one,constants+0x10,4);
    memcpy(&input.two,constants+0x9c,4);
    xv_object_marker_captured(guard);
    xv_object_math_unlock(&guard);guard=0;
    xv_marker_result result;
    xv_marker_snapshot(c,&input,&result);
    memcpy(output,&result.node,2);memcpy(output+4,result.local,52);
    memcpy(output+56,result.world,52);memcpy(stack,result.spills,32);
    return 1;
}
#endif
